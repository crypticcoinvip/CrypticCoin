// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Functionality for communicating with Tor.
 */
#ifndef BITCOIN_TORCONTROL_H
#define BITCOIN_TORCONTROL_H

#include <string>
#include <vector>
#include "scheduler.h"
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include "util.h"

namespace tor {

extern const std::string DEFAULT_TOR_CONTROL;
static const bool DEFAULT_LISTEN_ONION = true;

unsigned short const onion_port = 35089;

void StartTorControl(boost::thread_group& threadGroup, CScheduler& scheduler);
void InterruptTorControl();
void StopTorControl();

/**
 * Tor execution settings
 */
struct TorSettings {
    boost::filesystem::path tor_exe_path;
    boost::filesystem::path tor_obfs4_exe_path;
    std::vector<std::string> tor_bridges;
    bool tor_generate_config;
    unsigned short public_port;
    unsigned short hidden_port;
};

/**
* Create tor execution thread (execs tor, execs again if it gets closed)
* @param pathes are pathes to tor executables
*/
boost::optional<error_string> StartTor(const TorSettings& cfg);

/**
 * Kill tor previously executed by StartTor(). Uses tor.pid to locate the tor process.
 */
boost::optional<error_string> KillTor();

static boost::filesystem::path GetTorDir() {
    return GetDataDir() / "tor";
}

static boost::filesystem::path GetTorHiddenServiceDir() {
    return GetTorDir() / "hidden_service";
}

}

unsigned short GetTorServiceListenPort();

#endif /* BITCOIN_TORCONTROL_H */
