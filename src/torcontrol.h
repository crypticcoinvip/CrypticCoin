// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Functionality for communicating with Tor.
 */
#ifndef BITCOIN_TORCONTROL_H
#define BITCOIN_TORCONTROL_H

#include "scheduler.h"
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include "util.h"

namespace tor {

extern const std::string DEFAULT_TOR_CONTROL;
static const bool DEFAULT_LISTEN_ONION = true;

unsigned short const onion_port = 9089;

void StartTorControl(boost::thread_group& threadGroup, CScheduler& scheduler);
void InterruptTorControl();
void StopTorControl();

/**
 * Pathes which lead to tor executables
 */
struct TorExePathes {
    boost::filesystem::path tor_exe_path;
    boost::filesystem::path tor_obfs_exe_path;
};

/**
* Create tor execution thread (execs tor, execs again if it gets closed)
* @param tor_exe_path is path to tor executable
*/
boost::optional<error_string> StartTor(const TorExePathes& pathes);

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
