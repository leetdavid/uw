// Copyright 2026 The uw Authors. All rights reserved.

// uw's page translator, in place of Google's Translate Element. Chromium's
// translate.js drives it as google.translate.TranslateService. The translate
// agent asks uwTranslate.next() for the page's text in batches, has the
// browser translate each batch with the profile's translation engine, and
// hands the result to uwTranslate.deliver(). Each text segment is a run of
// text and inline elements written as HTML, with every element replaced by
// <a i=N>, so the engine can move the elements with the words. Text nodes are
// changed in place rather than replaced, so pages that keep references to
// them go on working. See docs/plans/page-translation.md.

var google = google || {};
google.translate = google.translate || {};

var uwTranslate = (function() {
  'use strict';

  const BATCH_SEGMENTS = 50;
  const BATCH_CHARS = 6000;
  const MAX_IN_FLIGHT = 4;
  const SETTLE_MS = 300;
  const MAX_RETRANSLATIONS = 3;
  const UNSUPPORTED_LANGUAGE = 2;

  const INLINE = new Set([
    'A', 'ABBR', 'B', 'BDI', 'BDO', 'BIG', 'CITE', 'DATA', 'DEL', 'DFN', 'EM',
    'FONT', 'I', 'INS', 'LABEL', 'MARK', 'Q', 'RP', 'RT', 'RUBY', 'S', 'SMALL',
    'SPAN', 'STRIKE', 'STRONG', 'SUB', 'SUP', 'TIME', 'TT', 'U',
  ]);
  const OPAQUE = new Set([
    'BR', 'CODE', 'IMG', 'INPUT', 'KBD', 'MATH', 'SAMP', 'SVG', 'VAR', 'WBR',
  ]);
  const SKIPPED = new Set([
    'AUDIO', 'CANVAS', 'EMBED', 'IFRAME', 'NOSCRIPT', 'OBJECT', 'PRE',
    'SCRIPT', 'STYLE', 'TEMPLATE', 'TEXTAREA', 'VIDEO',
  ]);
  const ATTRIBUTES = ['title', 'placeholder', 'alt', 'aria-label'];
  const RTL =
      new Set(['ar', 'dv', 'fa', 'he', 'iw', 'ps', 'sd', 'ug', 'ur', 'yi']);
  const LETTER = /\p{L}/u;

  let active = false;
  let source = '';
  let target = '';
  let onProgress = null;
  let initialLeft = 0;
  let nextId = 0;
  let queue = [];
  let queued = new Map();
  let inFlight = new Map();
  let waiters = [];
  let records = new Set();
  let owner = new WeakMap();
  let byParent = new Map();
  let attributes = new Map();
  let retranslations = new WeakMap();
  let dirtyRecords = new Set();
  let dirtyBlocks = new Set();
  let settleTimer = 0;
  let observer = null;
  let observedRoots = [];
  let rootLang = null;
  let rootDir = null;
  let applying = false;

  function isElement(node) {
    return node.nodeType === Node.ELEMENT_NODE;
  }

  function isText(node) {
    return node.nodeType === Node.TEXT_NODE;
  }

  function isNoTranslate(element) {
    return element.getAttribute('translate') === 'no' ||
        element.classList.contains('notranslate') || element.isContentEditable;
  }

  function isOpaque(element) {
    return OPAQUE.has(element.tagName.toUpperCase()) ||
        (INLINE.has(element.tagName) && isNoTranslate(element));
  }

  function isInline(element) {
    return INLINE.has(element.tagName) && !hasBlockInside(element);
  }

  function hasBlockInside(element) {
    for (const child of element.children) {
      const tag = child.tagName.toUpperCase();
      if (OPAQUE.has(tag)) {
        continue;
      }
      if (!INLINE.has(tag) || hasBlockInside(child)) {
        return true;
      }
    }
    return false;
  }

  function isSkipped(element) {
    return SKIPPED.has(element.tagName.toUpperCase()) ||
        (!INLINE.has(element.tagName) && isNoTranslate(element));
  }

  function blockOf(node) {
    let element = isElement(node) ? node : node.parentNode;
    while (element && isElement(element) && isInline(element)) {
      element = element.parentNode;
    }
    return element;
  }

  function hasLetters(run) {
    for (const node of run.nodes) {
      if (isText(node) ? LETTER.test(node.data) :
                         !isOpaque(node) && LETTER.test(node.textContent)) {
        return true;
      }
    }
    return false;
  }

  // Finds the runs of text and inline elements under `container` that are
  // not yet translated.
  function collect(container, runs) {
    if (isElement(container) && container.tagName === 'HEAD') {
      const title = container.querySelector('title');
      if (title) {
        collect(title, runs);
      }
      return;
    }
    let nodes = [];
    const flush = () => {
      const run = {parent: container, nodes};
      if (nodes.length && hasLetters(run) && !nodes.some(n => owner.has(n))) {
        runs.push(run);
      }
      nodes = [];
    };
    for (const child of container.childNodes) {
      if (isText(child)) {
        nodes.push(child);
      } else if (!isElement(child)) {
        continue;
      } else if (isOpaque(child) || isInline(child)) {
        nodes.push(child);
      } else {
        flush();
        if (!isSkipped(child)) {
          collect(child, runs);
        }
      }
    }
    flush();
    if (isElement(container) && container.shadowRoot) {
      collect(container.shadowRoot, runs);
      observe(container.shadowRoot);
    }
  }

  function collectAttributes(container, segments) {
    if (!isElement(container)) {
      return;
    }
    const elements = [container, ...container.querySelectorAll(
        ATTRIBUTES.map(name => `[${name}]`).join(','))];
    for (const element of elements) {
      if (isNoTranslate(element)) {
        continue;
      }
      for (const name of ATTRIBUTES) {
        const value = element.getAttribute(name);
        const record = attributes.get(element);
        if (value && LETTER.test(value) &&
            !(record && record.has(name) && record.get(name).shown === value)) {
          segments.push({element, name});
        }
      }
    }
  }

  function escape(text) {
    return text.replace(/&/g, '&amp;').replace(/</g, '&lt;')
        .replace(/>/g, '&gt;');
  }

  function serialize(nodes, map) {
    let html = '';
    for (const node of nodes) {
      if (isText(node)) {
        html += escape(node.data);
      } else if (isElement(node)) {
        const id = map.size;
        map.set(String(id), node);
        const inner = isOpaque(node) ? '' : serialize(node.childNodes, map);
        html += `<a i=${id}>${inner}</a>`;
      }
    }
    return html;
  }

  function textOf(html) {
    const document = new DOMParser().parseFromString(
        `<!doctype html><body>${html}`, 'text/html');
    return document.body.textContent;
  }

  function order(runs) {
    const height = window.innerHeight;
    const distance = run => {
      const element = isElement(run.parent) ? run.parent : run.parent.host;
      if (!element || !element.getClientRects().length) {
        return Infinity;
      }
      const rect = element.getBoundingClientRect();
      if (rect.bottom >= 0 && rect.top < height) {
        return 0;
      }
      return rect.top >= height ? rect.top - height : height - rect.bottom;
    };
    const keyed = runs.map(run => ({run, key: distance(run)}));
    keyed.sort((a, b) => a.key - b.key);
    return keyed.map(item => item.run);
  }

  function enqueue(segments) {
    for (const segment of segments) {
      const key = segment.nodes ? segment.nodes[0] : segment.element;
      const name = segment.name || '';
      const seen = queued.has(key) && queued.get(key).has(name);
      if (seen) {
        continue;
      }
      if (!queued.has(key)) {
        queued.set(key, new Set());
      }
      queued.get(key).add(name);
      queue.push(segment);
    }
    wake();
  }

  function takeBatch() {
    const batch = {id: ++nextId, segments: [], texts: [], initial: false};
    let chars = 0;
    while (queue.length && batch.segments.length < BATCH_SEGMENTS &&
           (chars < BATCH_CHARS || !batch.segments.length)) {
      const segment = queue.shift();
      const key = segment.nodes ? segment.nodes[0] : segment.element;
      queued.get(key)?.delete(segment.name || '');
      if (segment.nodes) {
        if (!stillThere(segment)) {
          if (segment.initial) {
            initialLeft--;
          }
          continue;
        }
        segment.map = new Map();
        segment.html = serialize(segment.nodes, segment.map);
        segment.texts = segment.nodes.filter(isText).map(n => n.data);
      } else {
        segment.value = segment.element.getAttribute(segment.name) || '';
        segment.html = escape(segment.value);
      }
      batch.initial = batch.initial || !!segment.initial;
      batch.segments.push(segment);
      batch.texts.push(segment.html);
      chars += segment.html.length;
    }
    return batch;
  }

  function stillThere(run) {
    return run.nodes.every(node => node.parentNode === run.parent) &&
        !run.nodes.some(node => owner.has(node));
  }

  function wake() {
    while (waiters.length && active && queue.length &&
           inFlight.size < MAX_IN_FLIGHT) {
      const batch = takeBatch();
      if (!batch.segments.length) {
        continue;
      }
      inFlight.set(batch.id, batch);
      waiters.shift()({
        id: batch.id,
        texts: batch.texts,
        source,
        target,
      });
    }
    if (!active) {
      for (const resolve of waiters.splice(0)) {
        resolve(null);
      }
    }
    finishIfDone();
  }

  function finishIfDone() {
    if (active && initialLeft === 0 && onProgress) {
      const report = onProgress;
      onProgress = null;
      report(100, true, false);
    }
  }

  function mutate(change) {
    applying = true;
    try {
      change();
    } finally {
      if (observer) {
        observer.takeRecords();
      }
      applying = false;
    }
  }

  // Builds the nodes a translated run becomes. Elements are the page's own;
  // texts are strings that place() puts into the page's text nodes.
  function build(parsedNodes, map, used, record) {
    const items = [];
    for (const parsed of parsedNodes) {
      if (isText(parsed)) {
        if (parsed.data) {
          items.push({text: parsed.data});
        }
        continue;
      }
      if (!isElement(parsed)) {
        continue;
      }
      const node =
          parsed.hasAttribute('i') && map.get(parsed.getAttribute('i'));
      if (node && !used.has(node)) {
        used.add(node);
        if (!isOpaque(node)) {
          place(node, [...node.childNodes], build(parsed.childNodes, map, used,
                                                  record), record);
        }
        items.push({node});
      } else {
        items.push(...build(parsed.childNodes, map, used, record));
      }
    }
    return items;
  }

  // Puts `items` into `container` in place of `nodes`, reusing the text nodes
  // among them in order.
  function place(container, nodes, items, record) {
    const texts = nodes.filter(isText);
    for (const item of items) {
      if (item.text === undefined) {
        continue;
      }
      let node = texts.shift();
      if (!node) {
        node = document.createTextNode('');
        record.created.add(node);
      }
      setText(node, item.text, record);
      item.node = node;
    }
    for (const node of texts) {
      setText(node, '', record);
    }
    const wanted = items.map(item => item.node);
    for (const node of nodes) {
      if (isElement(node) && !wanted.includes(node)) {
        wanted.push(node);
      }
    }
    const current = nodes.filter(node => wanted.includes(node));
    const same = !items.some(item => record.created.has(item.node)) &&
        current.length === wanted.length &&
        current.every((node, index) => node === wanted[index]);
    if (!same) {
      if (!record.orders.has(container)) {
        record.orders.set(container, [...container.childNodes]);
      }
      const anchor = nodes.length ? nodes[nodes.length - 1].nextSibling : null;
      for (const node of wanted) {
        container.insertBefore(node, anchor);
      }
    }
    for (const node of wanted) {
      owner.set(node, record);
    }
  }

  function setText(node, text, record) {
    if (!record.texts.has(node)) {
      record.texts.set(node, {source: node.data, shown: text});
    } else {
      record.texts.get(node).shown = text;
    }
    node.data = text;
    owner.set(node, record);
  }

  function applyRun(run, html) {
    const changed =
        run.nodes.filter(isText).some((node, i) => node.data !== run.texts[i]);
    if (!stillThere(run) || changed) {
      markDirtyBlock(blockOf(run.parent) || run.parent);
      return;
    }
    const parsed = new DOMParser().parseFromString(
        `<!doctype html><body>${html}`, 'text/html');
    const record = {
      parent: run.parent,
      nodes: run.nodes,
      texts: new Map(),
      created: new Set(),
      orders: new Map(),
    };
    mutate(() => {
      const used = new Set();
      const items = build(parsed.body.childNodes, run.map, used, record);
      place(run.parent, run.nodes, items, record);
    });
    records.add(record);
    if (!byParent.has(run.parent)) {
      byParent.set(run.parent, new Set());
    }
    byParent.get(run.parent).add(record);
  }

  function applyAttribute(segment, html) {
    if (segment.element.getAttribute(segment.name) !== segment.value) {
      return;
    }
    const text = textOf(html);
    if (!attributes.has(segment.element)) {
      attributes.set(segment.element, new Map());
    }
    const record = attributes.get(segment.element);
    if (!record.has(segment.name)) {
      record.set(segment.name, {source: segment.value, shown: text});
    } else {
      record.get(segment.name).shown = text;
    }
    mutate(() => segment.element.setAttribute(segment.name, text));
  }

  function restoreRecord(record) {
    for (const [node, text] of record.texts) {
      owner.delete(node);
      if (node.data === text.shown) {
        node.data = text.source;
      }
    }
    for (const node of record.created) {
      owner.delete(node);
      node.remove();
    }
    for (const [container, children] of record.orders) {
      if (container === record.parent) {
        continue;
      }
      for (const child of children) {
        if (child.parentNode === container) {
          container.appendChild(child);
        }
      }
    }
    if (record.orders.has(record.parent)) {
      const last = record.nodes[record.nodes.length - 1];
      let anchor = null;
      const children = record.orders.get(record.parent);
      const after = children.indexOf(last) + 1;
      for (const node of children.slice(after)) {
        if (node.parentNode === record.parent && !record.nodes.includes(node)) {
          anchor = node;
          break;
        }
      }
      for (const node of record.nodes) {
        if (node.parentNode === record.parent) {
          record.parent.insertBefore(node, anchor);
        }
      }
    }
    for (const node of record.nodes) {
      owner.delete(node);
    }
    records.delete(record);
    byParent.get(record.parent)?.delete(record);
  }

  function restoreAttributes() {
    for (const [element, names] of attributes) {
      for (const [name, value] of names) {
        if (element.getAttribute(name) === value.shown) {
          element.setAttribute(name, value.source);
        }
      }
    }
    attributes = new Map();
  }

  function markDirtyBlock(block) {
    if (!block) {
      return;
    }
    dirtyBlocks.add(block);
    scheduleSettle();
  }

  function scheduleSettle() {
    if (!settleTimer) {
      settleTimer = setTimeout(settle, SETTLE_MS);
    }
  }

  function onMutations(mutations) {
    if (applying || !active) {
      return;
    }
    for (const mutation of mutations) {
      const node = mutation.target;
      if (mutation.type === 'attributes') {
        const record = attributes.get(node);
        const value = record && record.get(mutation.attributeName);
        const now = node.getAttribute(mutation.attributeName);
        if (!value || now !== value.shown) {
          if (value) {
            record.delete(mutation.attributeName);
          }
          markDirtyBlock(node);
        }
        continue;
      }
      if (mutation.type === 'characterData') {
        const record = owner.get(node);
        if (record) {
          const text = record.texts.get(node);
          if (text && node.data !== text.shown) {
            text.source = node.data;
            text.shown = node.data;
            dirtyRecords.add(record);
          }
        } else {
          markDirtyBlock(blockOf(node));
        }
        continue;
      }
      const record = owner.get(node);
      if (record) {
        dirtyRecords.add(record);
      }
      for (const parentRecord of byParent.get(node) || []) {
        dirtyRecords.add(parentRecord);
      }
      for (const added of mutation.addedNodes) {
        if (isElement(added) && !isInline(added) && !isOpaque(added)) {
          markDirtyBlock(added);
        } else {
          markDirtyBlock(blockOf(node) || node);
        }
      }
    }
    scheduleSettle();
  }

  function settle() {
    settleTimer = 0;
    if (!active) {
      return;
    }
    const blocks = dirtyBlocks;
    dirtyBlocks = new Set();
    const changed = dirtyRecords;
    dirtyRecords = new Set();
    mutate(() => {
      for (const record of changed) {
        if (!records.has(record)) {
          continue;
        }
        const count = (retranslations.get(record.parent) || 0) + 1;
        retranslations.set(record.parent, count);
        if (count <= MAX_RETRANSLATIONS) {
          restoreRecord(record);
          blocks.add(blockOf(record.parent) || record.parent);
        }
      }
    });
    const runs = [];
    const segments = [];
    for (const block of blocks) {
      if (!block.isConnected ||
          (retranslations.get(block) || 0) > MAX_RETRANSLATIONS) {
        continue;
      }
      collect(block, runs);
      collectAttributes(block, segments);
    }
    enqueue([...runs, ...segments]);
  }

  function observe(root) {
    if (!observer || observedRoots.includes(root)) {
      return;
    }
    observedRoots.push(root);
    observer.observe(root, {
      subtree: true,
      childList: true,
      characterData: true,
      attributes: true,
      attributeFilter: ATTRIBUTES,
    });
  }

  function start(sourceLang, targetLang, progress) {
    if (active) {
      stop();
    }
    active = true;
    source = sourceLang;
    target = targetLang;
    onProgress = progress;
    observer = new MutationObserver(onMutations);
    observe(document.documentElement);
    const root = document.documentElement;
    rootLang = root.getAttribute('lang');
    rootDir = root.getAttribute('dir');
    mutate(() => {
      root.setAttribute('lang', target);
      if (RTL.has(target.split('-')[0])) {
        root.setAttribute('dir', 'rtl');
      }
    });
    const runs = [];
    collect(document.documentElement, runs);
    const segments = [];
    collectAttributes(document.documentElement, segments);
    const initial = [...order(runs), ...segments];
    for (const segment of initial) {
      segment.initial = true;
    }
    initialLeft = initial.length;
    enqueue(initial);
  }

  function stop() {
    active = false;
    clearTimeout(settleTimer);
    settleTimer = 0;
    if (observer) {
      observer.disconnect();
    }
    observer = null;
    observedRoots = [];
    applying = true;
    try {
      for (const record of [...records]) {
        restoreRecord(record);
      }
      restoreAttributes();
      const root = document.documentElement;
      for (const [name, value] of [['lang', rootLang], ['dir', rootDir]]) {
        if (value === null) {
          root.removeAttribute(name);
        } else {
          root.setAttribute(name, value);
        }
      }
    } finally {
      applying = false;
    }
    records = new Set();
    owner = new WeakMap();
    byParent = new Map();
    queue = [];
    queued = new Map();
    inFlight = new Map();
    dirtyRecords = new Set();
    dirtyBlocks = new Set();
    onProgress = null;
    initialLeft = 0;
    wake();
  }

  function fail(error) {
    const report = onProgress;
    onProgress = null;
    if (report) {
      report(0, false, error === UNSUPPORTED_LANGUAGE ? error : true);
    } else {
      stop();
    }
  }

  return {
    // Resolves with the next batch to translate, or null once the page is
    // shown in its own language again.
    next() {
      return new Promise(resolve => {
        waiters.push(resolve);
        wake();
      });
    },

    // Puts the translations of batch `id` into the page. `error` is 0, or
    // 2 when the engine does not know the page's language.
    deliver(id, translations, error) {
      const batch = inFlight.get(id);
      if (!batch || !active) {
        return;
      }
      inFlight.delete(id);
      if (error || !translations ||
          translations.length !== batch.segments.length) {
        if (batch.initial) {
          fail(error);
          return;
        }
      } else {
        batch.segments.forEach((segment, index) => {
          if (segment.nodes) {
            applyRun(segment, translations[index]);
          } else {
            applyAttribute(segment, translations[index]);
          }
        });
      }
      initialLeft -= batch.segments.filter(segment => segment.initial).length;
      wake();
    },

    start,
    stop,
    get source() {
      return source;
    },
  };
})();

google.translate.TranslateService = function() {
  return {
    isAvailable() {
      return true;
    },
    translatePage(sourceLang, targetLang, onProgress) {
      uwTranslate.start(sourceLang, targetLang, onProgress);
    },
    restore() {
      uwTranslate.stop();
    },
    getDetectedLanguage() {
      return uwTranslate.source === 'auto' ? 'und' : uwTranslate.source;
    },
  };
};

cr.googleTranslate.onTranslateElementLoad();
