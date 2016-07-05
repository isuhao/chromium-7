// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/renderer/user_script_set_manager.h"

#include "components/crx_file/id_util.h"
#include "content/public/renderer/render_thread.h"
#include "extensions/common/extension_messages.h"
#include "extensions/renderer/dispatcher.h"
#include "extensions/renderer/script_injection.h"
#include "extensions/renderer/user_script_set.h"
#include "ipc/ipc_message.h"
#include "ipc/ipc_message_macros.h"

namespace extensions {

UserScriptSetManager::UserScriptSetManager() {
  content::RenderThread::Get()->AddObserver(this);
}

UserScriptSetManager::~UserScriptSetManager() {
}

void UserScriptSetManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void UserScriptSetManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

std::unique_ptr<ScriptInjection>
UserScriptSetManager::GetInjectionForDeclarativeScript(
    int script_id,
    content::RenderFrame* render_frame,
    int tab_id,
    const GURL& url,
    const std::string& extension_id) {
  UserScriptSet* user_script_set =
      GetProgrammaticScriptsByHostID(HostID(HostID::EXTENSIONS, extension_id));
  if (!user_script_set)
    return std::unique_ptr<ScriptInjection>();

  return user_script_set->GetDeclarativeScriptInjection(
      script_id,
      render_frame,
      tab_id,
      UserScript::BROWSER_DRIVEN,
      url);
}

bool UserScriptSetManager::OnControlMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(UserScriptSetManager, message)
    IPC_MESSAGE_HANDLER(ExtensionMsg_UpdateUserScripts, OnUpdateUserScripts)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void UserScriptSetManager::GetAllInjections(
    std::vector<std::unique_ptr<ScriptInjection>>* injections,
    content::RenderFrame* render_frame,
    int tab_id,
    UserScript::RunLocation run_location) {
  static_scripts_.GetInjections(injections, render_frame, tab_id, run_location);
  for (UserScriptSetMap::iterator it = programmatic_scripts_.begin();
       it != programmatic_scripts_.end();
       ++it) {
    it->second->GetInjections(injections, render_frame, tab_id, run_location);
  }
}

void UserScriptSetManager::GetAllActiveExtensionIds(
    std::set<std::string>* ids) const {
  DCHECK(ids);
  static_scripts_.GetActiveExtensionIds(ids);
  for (UserScriptSetMap::const_iterator it = programmatic_scripts_.begin();
       it != programmatic_scripts_.end();
       ++it) {
    it->second->GetActiveExtensionIds(ids);
  }
}

UserScriptSet* UserScriptSetManager::GetProgrammaticScriptsByHostID(
    const HostID& host_id) {
  UserScriptSetMap::const_iterator it = programmatic_scripts_.find(host_id);
  return it != programmatic_scripts_.end() ? it->second.get() : NULL;
}

void UserScriptSetManager::OnUpdateUserScripts(
    base::SharedMemoryHandle shared_memory,
    const HostID& host_id,
    const std::set<HostID>& changed_hosts,
    bool whitelisted_only) {
  if (!base::SharedMemory::IsHandleValid(shared_memory)) {
    NOTREACHED() << "Bad scripts handle";
    return;
  }

  for (const HostID& host_id : changed_hosts) {
    if (host_id.type() == HostID::EXTENSIONS &&
        !crx_file::id_util::IdIsValid(host_id.id())) {
      NOTREACHED() << "Invalid extension id: " << host_id.id();
      return;
    }
  }

  UserScriptSet* scripts = NULL;
  if (!host_id.id().empty()) {
    // The expectation when there is a host that "owns" this shared
    // memory region is that the |changed_hosts| is either the empty list
    // or just the owner.
    CHECK(changed_hosts.size() <= 1);
    if (programmatic_scripts_.find(host_id) == programmatic_scripts_.end()) {
      scripts = new UserScriptSet();
      programmatic_scripts_[host_id] = make_linked_ptr(scripts);
    } else {
      scripts = programmatic_scripts_[host_id].get();
    }
  } else {
    scripts = &static_scripts_;
  }
  DCHECK(scripts);

  // If no hosts are included in the set, that indicates that all
  // hosts were updated. Add them all to the set so that observers and
  // individual UserScriptSets don't need to know this detail.
  const std::set<HostID>* effective_hosts = &changed_hosts;
  std::set<HostID> all_hosts;
  if (changed_hosts.empty()) {
    // The meaning of "all hosts(extensions)" varies, depending on whether some
    // host "owns" this shared memory region.
    // No owner => all known hosts.
    // Owner    => just the owner host.
    if (host_id.id().empty()) {
      std::set<std::string> extension_ids =
          RendererExtensionRegistry::Get()->GetIDs();
      for (const std::string& extension_id : extension_ids)
        all_hosts.insert(HostID(HostID::EXTENSIONS, extension_id));
    } else {
      all_hosts.insert(host_id);
    }
    effective_hosts = &all_hosts;
  }

  if (scripts->UpdateUserScripts(shared_memory,
                                 *effective_hosts,
                                 whitelisted_only)) {
    FOR_EACH_OBSERVER(
        Observer,
        observers_,
        OnUserScriptsUpdated(*effective_hosts, scripts->scripts()));
  }
}

}  // namespace extensions