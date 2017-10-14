#pragma once
  
#include <string>
#include "sql_db.h"
#include "resource_manager.h"
#include "build_queue.h"

namespace builder {
  class BuildQueue {

  public:
    // Constructors
    BuildQueue();
    ~BuildQueue();
    BuildQueue(const BuildQueue&)            = delete;
    BuildQueue& operator=(const BuildQueue&) = delete;
    BuildQueue(BuildQueue&&) noexcept        = delete;
    BuildQueue& operator=(BuildQueue&&)      = delete;

    void reserve_build_slot();
  private:
    SQL db;
    ResourceManager resources;
    const std::string build_id;
    void exit();
    bool top();
  };
}
