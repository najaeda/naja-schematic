#pragma once
#ifndef __EMSCRIPTEN__

#include "INetlistProvider.h"
#include "Types.h"
#include <string>
#include <vector>
#include <unordered_map>

struct VPort {
  std::string name;
  Direction   direction {Direction::Input};
  int         msb {-1};
  int         lsb {-1};
};

struct VInstance {
  std::string instanceName;
  std::string moduleName;
  unsigned    childId {0};
};

struct VModule {
  std::string           name;
  unsigned              designId {0};
  std::vector<VPort>    ports;
  std::vector<VInstance> instances;   // cells whose type is a known module
  std::vector<VInstance> primitives;  // cells whose type is a library/black-box cell
};

// INetlistProvider implementation for standalone desktop mode.
// Parses Verilog/SystemVerilog via yosys and answers tree requests synchronously.
class LocalSNLProvider : public INetlistProvider {
  public:
    LocalSNLProvider() = default;

    void send(const std::string& msg) override;
    void on_open(std::function<void()> callback) override;
    void on_message(std::function<void(const std::string&)> callback) override;
    void on_close(std::function<void()> callback) override;
    void on_error(std::function<void(const std::string&)> callback) override;

    void start() override;
    void loadFile(const std::string& path) override;

  private:
    std::function<void()>                   openCb_;
    std::function<void(const std::string&)> msgCb_;
    std::function<void()>                   closeCb_;
    std::function<void(const std::string&)> errCb_;

    std::string                              netlibPath_;
    std::vector<VModule>                     modules_;
    std::unordered_map<std::string, unsigned> moduleIndex_; // name -> index in modules_
    unsigned                                 topModuleIdx_ {0};

    void        parseYosysJson(const std::string& jsonPath);
    void        handleRequest(const std::string& jsonRequest);
    std::string buildRootResponse() const;
    std::string buildInstancesResponse(unsigned guiId, unsigned designId, bool primitives) const;
    std::string buildTermsResponse(unsigned guiId, unsigned designId) const;
};

#endif // __EMSCRIPTEN__
