/*
 * Copyright (C) 2018 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <gflags/gflags.h>

#include <ignition/common/Console.hh>
#include <ignition/common/SignalHandler.hh>
#include <ignition/common/Time.hh>

#include <csignal>
#include <iostream>

#include "ignition/gazebo/config.hh"

// Gflag command line argument definitions
// This flag is an abbreviation for the longer gflags built-in help flag.
DEFINE_bool(h, false, "");
DEFINE_int32(verbose, 1, "");
DEFINE_int32(v, 1, "");
DEFINE_double(z, -1, "Update rate in Hertz.");
DEFINE_uint64(iterations, 0, "Number of iterations to execute.");
DEFINE_bool(s, false, "Run only the server (headless mode).");
DEFINE_bool(g, false, "Run only the GUI.");
DEFINE_string(f, "", "Load an SDF file on start.");
DEFINE_bool(r, false, "Run simulation on start. "
    "The default is false, which starts simulation paused.");

//////////////////////////////////////////////////
void Help()
{
  std::cout
  << "ign-gazebo -- Run the Gazebo server and GUI." << std::endl
  << std::endl
  << "`ign-gazebo` [options] <world_file>" << std::endl
  << std::endl
  << std::endl
  << "Options:" << std::endl
  << "  -h [ --help ]          Print help message."
  << std::endl
  << "  --version              Print version information."
  << std::endl
  << "  -v [--verbose] arg     Adjust the level of console output (0~4)."
  << " The default verbosity is 1"
  << std::endl
  << "  --iterations arg       Number of iterations to execute."
  << std::endl
  << "  -s                     Run only the server (headless mode). This will "
  << " override -g, if it is also present."
  << std::endl
  << "  -g                     Run only the GUI."
  << std::endl
  << "  -f                     Load an SDF file on start. "
  << std::endl
  << "  -z arg                 Update rate in Hertz."
  << std::endl
  << "  -r                     Run simulation on start."
  << " The default is false, which starts simulation paused."
  << std::endl
  << std::endl;
}

//////////////////////////////////////////////////
void Version()
{
  std::cout << IGNITION_GAZEBO_VERSION_HEADER << std::endl;
}

//////////////////////////////////////////////////
static bool VerbosityValidator(const char */*_flagname*/, int _value)
{
  return _value >= 0 && _value <= 4;
}

//////////////////////////////////////////////////
/// \brief Try to kill a single process.
/// \param[in] _pid Process ID.
/// \param[in] _name Process name.
/// \param[in] _timeout Total time to wait in seconds.
/// \param[in, out] _killed Set to true if process was successfully killed.
static void KillProcess(pid_t _pid, const std::string &_name,
                        const double _timeout, bool &_killed)
{
  kill(_pid, SIGINT);

  // Wait some time and if not dead, escalate to SIGKILL
  double sleepSecs = 0.001;
  int status;
  for (unsigned int i = 0; i < (unsigned int)(_timeout / sleepSecs); ++i)
  {
    if (_killed)
    {
      break;
    }
    else
    {
      int p = waitpid(_pid, &status, WNOHANG);
      if (p == _pid)
      {
        _killed = true;
        break;
      }
    }
    // Sleep briefly
    ignition::common::Time::Sleep(ignition::common::Time(sleepSecs));
  }
  if (!_killed)
  {
    ignerr << "Escalating to SIGKILL on [" << _name << "]" << std::endl;
    kill(_pid, SIGKILL);
  }
}

//////////////////////////////////////////////////
int main(int _argc, char **_argv)
{
  int returnValue = 0;

  // Store all arguments for child processes before gflags clears them
  char **argvServer = new char*[_argc+1];
  char **argvGui = new char*[_argc+1];
  argvServer[0] = const_cast<char *>("ign-gazebo-server");
  argvGui[0] = const_cast<char *>("ign-gazebo-gui");
  for (int i = 1; i < _argc; ++i)
  {
    argvServer[i] = _argv[i];
    argvGui[i] = _argv[i];
  }
  argvServer[_argc] = static_cast<char *>(nullptr);
  argvGui[_argc] = static_cast<char *>(nullptr);

  // Register validators
  gflags::RegisterFlagValidator(&FLAGS_verbose, &VerbosityValidator);
  gflags::RegisterFlagValidator(&FLAGS_v, &VerbosityValidator);

  // Parse command line
  gflags::AllowCommandLineReparsing();
  gflags::ParseCommandLineNonHelpFlags(&_argc, &_argv, true);

  // Hold info as we parse it
  gflags::CommandLineFlagInfo info;

  // Help
  // Parse out the help flag in such a way that the full help text
  // is suppressed: if --help* or -h is specified, override the default
  // help behavior and turn on --helpmatch, to only shows help for the
  // current executable (instead of showing a huge list of gflags built-ins).
  gflags::GetCommandLineFlagInfo("help", &info);
  bool showHelp = FLAGS_h || (info.current_value == "true");

  // Version
  gflags::GetCommandLineFlagInfo("version", &info);
  bool showVersion = (info.current_value == "true");

  // Verbosity
  gflags::GetCommandLineFlagInfo("verbose", &info);
  if (info.is_default)
  {
    gflags::GetCommandLineFlagInfo("v", &info);
    if (!info.is_default)
      FLAGS_verbose = FLAGS_v;
    else
      FLAGS_verbose = 1;
  }

  // If help message is requested, substitute in the override help function.
  if (showHelp)
  {
    gflags::SetCommandLineOption("help", "false");
    gflags::SetCommandLineOption("helpshort", "false");
    gflags::SetCommandLineOption("helpfull", "false");
    gflags::SetCommandLineOption("helpmatch", "");
    Help();
    return returnValue;
  }

  // If version is requested, override with custom version print function.
  if (showVersion)
  {
    gflags::SetCommandLineOption("version", "false");
    Version();
    return returnValue;
  }

  // Run Gazebo
  ignition::common::Console::SetVerbosity(FLAGS_verbose);
  ignmsg << "Ignition Gazebo        v" << IGNITION_GAZEBO_VERSION_FULL
         << std::endl;

  // Run the server
  pid_t serverPid;
  if (!FLAGS_g)
  {
    serverPid = fork();
    if (serverPid == 0)
    {
      // remove client from foreground process group
      setpgid(serverPid, 0);

      // Spin up server process and block here
      execvp(argvServer[0], argvServer);
    }
  }

  // Run the GUI
  pid_t  guiPid;
  if (!FLAGS_s)
  {
    guiPid = fork();
    if (guiPid == 0)
    {
      // remove client from foreground process group
      setpgid(guiPid, 0);

      // Spin up GUI process and block here
      execvp(argvGui[0], argvGui);
    }
  }

  // Signal handler
  ignition::common::SignalHandler sigHandler;
  bool guiKilled = false;
  bool serverKilled = false;
  bool sigKilled = false;
  sigHandler.AddCallback([&](const int /*_sig*/)
  {
    sigKilled = true;
    if (!FLAGS_s)
      KillProcess(guiPid, "ign-gazebo-gui", 5.0, guiKilled);
    if (!FLAGS_g)
      KillProcess(serverPid, "ign-gazebo-server", 5.0, serverKilled);
  });

  // Block until one of the processes ends
  int child_exit_status;
  pid_t deadChild = wait(&child_exit_status);

  // Check dead process' return value
  if ((WIFEXITED(child_exit_status) == 0) ||
      (WEXITSTATUS(child_exit_status) != 0))
    returnValue = -1;
  else
    returnValue = 0;

  if (deadChild == guiPid)
    guiKilled = true;
  else if (deadChild == serverPid)
    serverKilled = true;

  // one of the children died
  if (!sigKilled)
    std::raise(SIGINT);

  delete[] argvServer;
  delete[] argvGui;

  igndbg << "Shutting down ign-gazebo" << std::endl;
  return returnValue;
}
