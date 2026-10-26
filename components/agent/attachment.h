// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_COMPONENTS_AGENT_ATTACHMENT_H_
#define UW_COMPONENTS_AGENT_ATTACHMENT_H_

#include <cstdint>
#include <string>

namespace uw {

// A file the user attached to a message, as the model gets it. uw keeps a
// copy in the chat's folder, where the agent's terminal opens it. See
// docs/adr/0020-attached-files-are-copies-the-agent-reaches-by-path.md.
struct Attachment {
  Attachment();
  Attachment(const Attachment&);
  Attachment(Attachment&&);
  Attachment& operator=(const Attachment&);
  Attachment& operator=(Attachment&&);
  ~Attachment();

  // The name the user knows it by, and the path of its copy.
  std::string name;
  std::string path;
  std::string type;
  int64_t size = 0;
  // For an image the model sees: the scaled copy, as a data: URL.
  std::string image;
  // What the model reads as text: a text file, or a PDF's text, with a line
  // saying where it stops short of the file.
  std::string text;
  // For a PDF the model may read whole: the PDF as a data: URL.
  std::string pdf;
  int pages = 0;
  // A sentence for the model after the file's path, without its full stop,
  // such as why it gets only the path.
  std::string note;
};

}  // namespace uw

#endif  // UW_COMPONENTS_AGENT_ATTACHMENT_H_
