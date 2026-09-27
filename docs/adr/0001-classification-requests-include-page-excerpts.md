# Classification requests include page excerpts

Automatic organization has to classify pages whose titles and URLs carry little meaning. We decided that each [classification call](../CONTEXT.md#language) includes a short readable-text excerpt from the page, even when the connection is a remote provider. This supersedes the earlier plan to defer excerpts pending a privacy decision: the user chose accuracy over payload minimalism for the MVP, and the setup dialog shows this exact scope before enabling.

Considered options: metadata only (rejected — too many tabs would stay unfiled on opaque pages); excerpts for local endpoints only (rejected — two request shapes to maintain).
