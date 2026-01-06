// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Part of txtempus, a LF time signal transmitter.
// Control socket for runtime weather updates

#ifndef DCF77_CONTROL_H
#define DCF77_CONTROL_H

#include <string>

#include "time-signal-source.h"

// Control FIFO for runtime weather updates.
// Uses a named pipe (FIFO) for simple usage: echo 'command' > /tmp/txtempus.fifo
// Responses are printed to stderr.
// Changes are staged and apply at the start of the next 3-minute cycle
// to avoid corrupting the currently transmitting region's data.
//
// Commands:
//   set <region> <day> <night> <temp_day> <temp_night> [extreme] [rain] [wind_dir] [wind_bft]
//   set_all <day> <night> <temp_day> <temp_night> [extreme] [rain] [wind_dir] [wind_bft]
//   reset
//   status
//   list
//   help
//
class DCF77ControlSocket {
 public:
  explicit DCF77ControlSocket(DCF77WeatherTimeSignalSource* source);
  ~DCF77ControlSocket();

  bool Start(const std::string& fifo_path = "/tmp/txtempus.fifo");
  void Stop();
  void Poll();

  const std::string& GetSocketPath() const { return socket_path_; }

 private:
  void ProcessCommand(const std::string& cmd);

  DCF77WeatherTimeSignalSource* source_;
  std::string socket_path_;
  int server_fd_ = -1;
  int client_fd_ = -1;
};

#endif  // DCF77_CONTROL_H
