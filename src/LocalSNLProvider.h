#pragma once
#ifndef __EMSCRIPTEN__

#include "INetlistProvider.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <filesystem>

namespace naja::NL {
  class NLDB;
}

// INetlistProvider for standalone desktop mode.
// Backs the tree with naja SNL objects loaded directly in-process.
class LocalSNLProvider : public INetlistProvider {
  public:
    LocalSNLProvider();
    ~LocalSNLProvider() override;

    void send(const std::string& msg) override;
    void on_open(std::function<void()> callback) override;
    void on_message(std::function<void(const std::string&)> callback) override;
    void on_close(std::function<void()> callback) override;
    void on_error(std::function<void(const std::string&)> callback) override;
    void start() override;

    // Load a pre-built SNL database (Cap'n Proto serialized directory).
    void loadSNL(const std::string& path);

    // Load structural Verilog.  Liberty files define the primitive cell library.
    void loadVerilog(const std::vector<std::string>& verilogFiles,
                     const std::vector<std::string>& libertyFiles);

    // Load SystemVerilog via slang.
    // Each entry in sources is either a .sv/.v file or a .f/.flist file;
    // Flist entries are expanded recursively before passing to the constructor.
    void loadSystemVerilog(const std::vector<std::string>& sources);

  private:
    std::function<void()>                   openCb_;
    std::function<void(const std::string&)> msgCb_;
    std::function<void()>                   closeCb_;
    std::function<void(const std::string&)> errCb_;

    naja::NL::NLDB* db_ {nullptr};

    // Reset the SNL universe to a clean state and return a fresh NLDB.
    naja::NL::NLDB* freshDB();

    // Find the top design among user libraries and register it in the DB.
    void findAndSetTop();

    void        handleRequest(const std::string& jsonRequest);
    std::string buildRootResponse() const;
    std::string buildInstancesResponse(unsigned guiId, unsigned dbId,
                                       unsigned libId, unsigned designId,
                                       bool primitives) const;
    std::string buildTermsResponse(unsigned guiId, unsigned dbId,
                                   unsigned libId, unsigned designId) const;
    std::string buildEquipotentialResponse(const nlohmann::json& req) const;
};

#endif // __EMSCRIPTEN__
