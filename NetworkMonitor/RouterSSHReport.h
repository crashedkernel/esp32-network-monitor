#pragma once
#include "GLClientStream.h"
#include <stdint.h>

// Transport-independent state for SSH callbacks. Data is consumed immediately,
// and a report requires complete neighbor output and successful process exit.
class RouterSSHReport {
public:
  GLClientStream parser;
  uint32_t bytes = 0, stderrBytes = 0;
  bool eof = false, closed = false, exitReceived = false;
  int exitStatus = -1;
  const char *failure = nullptr;
  char stderrText[161] = {};
  unsigned stderrUsed = 0;
  explicit RouterSSHReport(RouterSnapshot &s) : parser(s) {}
  void data(const char *buffer, unsigned length, bool isStderr) {
    if (isStderr) {
      stderrBytes += length;
      for (unsigned i = 0; i < length && stderrUsed < sizeof(stderrText) - 1; i++) {
        unsigned char c = buffer[i];
        if (c == '\r' || c == '\n' || c == '\t')
          c = ' ';
        if (c >= 32 && c < 127)
          stderrText[stderrUsed++] = c;
      }
      stderrText[stderrUsed] = 0;
      if (stderrBytes > 4096 && !failure)
        failure = "Router returned excessive error output";
      return;
    }
    bytes += length;
    if (bytes > 2097152 && !failure)
      failure = "Router report exceeds 2 MB";
    if (failure)
      return;
    for (unsigned i = 0; i < length; i++)
      if (!parser.feed(buffer[i])) {
        failure = parser.error();
        break;
      }
  }
  bool terminal() const { return (eof || closed) && exitReceived; }
  bool valid() { return !failure && terminal() && exitStatus == 0 && parser.finish(); }
};
