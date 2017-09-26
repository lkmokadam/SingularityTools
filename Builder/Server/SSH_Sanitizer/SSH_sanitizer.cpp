#include <iostream>
#include <string>
#include <sstream>
#include <errno.h>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/process.hpp>
#include <csignal>
#include <system_error>

// Execute a sanitized copy of $SSH_ORIGINAL_COMMAND, as provided by the ssh authorized_keys command option
// A single string argument is expected so it must be quoted when setting command, e.g.
// command="/path/to/CommandSanitizer \"${SSH_ORIGINAL_COMMAND}\"" ssh-rsa ...
// the SSH_CONNECTION info is used as a unique identifier to ensure users only have very limited access

// The following commands are allowed:
// scp -t unique_work_path()/container.def
// scp -f unique_work_path()/container.img
// GetWorkPath

// File namespace(static)
namespace {
  constexpr auto gBuilderBase = "/usr/local/bin/SingularityBuilder";
  constexpr auto gScpBase = "scp";
  constexpr auto gBuilderDirectoryPath = "/home/builder/container_scratch/";
  constexpr auto gGetWorkPath = "GetWorkPath";

  volatile std::sig_atomic_t gShouldKill = 0;

  void signal_handler(int signal) {
    gShouldKill = 1;
  }

  // Preform a blocking exec
  int blocking_exec(std::string command) {
    namespace bp = boost::process;

    bp::environment env;   // Blank environment env
    int return_code;

    // Launch the command asynchronously
    bp::child child_proc(command, env);
    // Test if we should terminate the command
    // This can be set by signal handlers
    while(child_proc.running()) {
      if(gShouldKill) {
        pid_t pid = child_proc.id();
        kill(pid, SIGINT);
      }
    }

    // Wait for child to complete
    child_proc.wait();
    return_code = child_proc.exit_code();

    return return_code;
  }

  // Get the temporary unique build directory, based on concating the SSH_CONNECTION env var
  std::string unique_work_path() {
    char const* tmp = getenv("SSH_CONNECTION");
    if ( tmp == NULL ) {
      throw std::system_error(EIDRM, std::generic_category(), "SSH_CONNECTION");
    }
 
    std::string SSH_CONNECTION(tmp);

    // Remove any leading/trailing white space
    boost::trim(SSH_CONNECTION);

    // Test for the correct number of components
    std::vector<std::string> split_connection;
    boost::split(split_connection, SSH_CONNECTION, boost::is_any_of("\t "), boost::token_compress_on);
    if(split_connection.size() != 4) {
      throw std::system_error(EINVAL, std::generic_category(), "SSH_CONNECTION");
    }

    // Replace the spaces with underscores
    boost::replace_all(SSH_CONNECTION, " ", "_");

    return gBuilderDirectoryPath + SSH_CONNECTION;
  }

  void builder_prep() {
    boost::filesystem::create_directories(unique_work_path());
  }

  int builder_run() {
    std::string builder_call{gBuilderBase};
    builder_call += " " + unique_work_path() + " " + unique_work_path();
    int err = blocking_exec(builder_call);
    return err;
  }

  void builder_cleanup() {
    boost::filesystem::remove_all(unique_work_path());
  }

  // We allow exactly two scp cases, the command passed to SSH_ORIGINAL_COMMAND is NOT the same is is run on the client
  // scp -t unique_work_path()/container.def
  // scp -f unique_work_path()/container.img
  // Upon transfering the definition to the builder we create the unique directory and kick off the build process
  // Upon transfering the final image to the client we delete the unique work directory o nthe builder 
  int run_scp(const std::vector<std::string>& split_command) {
    // Check number of arguments
    if(split_command.size() != 3) {
      throw std::system_error(EINVAL, std::generic_category(), "scp");
    }

    int err;
    if(split_command[1] == "-t") {
      // Initialize work area
      builder_prep();

      // Initiate SCP
      std::string scp_call{gScpBase};
      scp_call += " -t " + unique_work_path() + "/container.def";
      err = blocking_exec(scp_call);
      if(err)
        return err;
     
      // Kick off builder
      err = builder_run();
    } 
    else if(split_command[1] == "-f") {
      // Initiate SCP
      std::string scp_call{gScpBase};
      scp_call += " -f " + unique_work_path() + "/container.img";
      err = blocking_exec(scp_call);
 
      // Cleanup work area
      builder_cleanup();
    }
    else {
      throw std::system_error(EINVAL, std::generic_category(), "scp");
    }
    return err;
  }

} // End anonymous namespace

int main(int argc, char** argv) {  
  // A single string argument is required
  if(argc != 2) {
    throw std::system_error(EINVAL, std::generic_category(), "SSH_Sanitizer");
  }

  // Create string from char* argument
  std::string command(argv[1]);

  // Quick sanity check for any invalid special characters
  std::string invalid_chars("!%^*~|;(){}[]$#\\");
  for(char c : invalid_chars) {
    if(command.find(c) != std::string::npos) {
      throw std::system_error(EINVAL, std::generic_category(), "SSH_CONNECTION");
    }
  }

  // Remove any leading/trailing white space
  boost::trim(command);

  // Split the string command on space(s) or tab(s)
  std::vector<std::string> split_command;
  boost::split(split_command, command, boost::is_any_of("\t "), boost::token_compress_on);

  // Register signal handlers
  std::signal(SIGABRT, signal_handler);
  std::signal(SIGBUS,  signal_handler);
  std::signal(SIGHUP,  signal_handler);
  std::signal(SIGILL,  signal_handler);
  std::signal(SIGINT,  signal_handler);
  std::signal(SIGPIPE, signal_handler);
  std::signal(SIGQUIT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  int err;

  try {
    if(split_command[0] == gScpBase){
      err = run_scp(split_command);
    }
    else if(split_command[0] == gGetWorkPath) {
      std::cout<<unique_work_path()<<std::endl;
      err = 0;
    }
    else {
      throw std::system_error(ENOSYS, std::generic_category(), "SSH_Sanitizer");
    }
  } catch(const std::system_error& error) {
      std::cout << "ERROR: " << error.code() << " - " << error.what() << std::endl;
      err = error.code().value();
      builder_cleanup();
  } catch(const boost::filesystem::filesystem_error& error) {
      std::cout << "ERROR: " << error.what() << std::endl;
      err = EXIT_FAILURE;
      builder_cleanup();
  }

  return err;
}
