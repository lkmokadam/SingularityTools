#include "vagrant_builder.h"
#include <iostream>
#include <string>
#include <system_error>
#include "sql_db.h"
#include "signal_handler.h"
#include <boost/process.hpp>
#include "build_queue.h"

namespace builder {
  namespace bp = boost::process;

  // Upon construction the build will be added to the queue
  VagrantBuilder::VagrantBuilder() : active{false} {
  }

  VagrantBuilder::~VagrantBuilder() {
    if(this->active)
      this->destroy();
  }

  // Stop the vagrant VM and remove it
  void VagrantBuilder::destroy() {
    std::cerr<<"Attempting to destroy VM..."<<std::endl;
    std::string stop_command("vagrant destroy");
    std::error_code err;
    boost::process::system(stop_command, err);
    if(err.value() != 0) {
      std::cerr<<"Failed to stop Vagrant VM!"<<std::endl;
    }
    this->active = false;
  }

  // Wait for an open build spot and then Fire up the VM
  void VagrantBuilder::bring_up() {
    std::string vagrant_up_command;
    vagrant_up_command += "vagrant up";

    // Launch the command asynchronously
    bp::child vagrant_proc(vagrant_up_command);

    // Test if we should stop vagrant
    while(vagrant_proc.running()) {
      if(gShouldKill) {
        destroy();
      }
    }

    // Wait for vagrant to complete bool
    vagrant_proc.wait();
    int rc = vagrant_proc.exit_code();
    if(rc == 0)
      this->active = true;
    else {
      this->destroy();
      throw std::system_error(ENONET, std::generic_category(), "Vagrant bring_up failed!");
    }
      
  }

  // Run singularity build within our vagrant container
  int VagrantBuilder::build() {
    // Wait for a valid slot to become available
    BuildQueue queue;
    queue.reserve_build_slot();

    // Spin up the VM if slot is available
    this->bring_up();

    // Launch the vagrant build asynchronously
    std::string vagrant_build_command("vagrant ssh -c 'sudo singularity build /vagrant/container.img /vagrant/container.def'");
    bp::child vagrant_proc_build(vagrant_build_command);

    // Test if we should stop vagrant
    while(vagrant_proc_build.running()) {
      if(gShouldKill) {
        destroy();
      }
    }

    // Wait for vagrant to complete
    vagrant_proc_build.wait();

    this->destroy();

    return vagrant_proc_build.exit_code();
  }
}
