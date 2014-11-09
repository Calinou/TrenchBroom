/*
 Copyright (C) 2010-2014 Kristian Duske
 
 This file is part of TrenchBroom.
 
 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "KeyboardShortcutEntry.h"

#include "View/ActionContext.h"
#include "View/KeyboardShortcut.h"

namespace TrenchBroom {
    namespace View {
        KeyboardShortcutEntry::KeyboardShortcutEntry() : m_hasConflicts(false) {}

        KeyboardShortcutEntry::~KeyboardShortcutEntry() {}
        
        bool KeyboardShortcutEntry::modifiable() const {
            return doGetModifiable();
        }

        int KeyboardShortcutEntry::requiredModifiers() const {
            return doGetRequiredModifiers();
        }

        bool KeyboardShortcutEntry::hasConflicts() const {
            return m_hasConflicts;
        }
        
        void KeyboardShortcutEntry::resetConflicts() {
            m_hasConflicts = false;
        }

        bool KeyboardShortcutEntry::updateConflicts(const KeyboardShortcutEntry* entry) {
            const bool conflicts = conflictsWith(entry);
            m_hasConflicts |= conflicts;
            return conflicts;
        }
        
        bool KeyboardShortcutEntry::conflictsWith(const KeyboardShortcutEntry* entry) const {
            if (entry == this)
                return false;
            
            if ((actionContext() & entry->actionContext()) == 0)
                return false;
            
            const KeyboardShortcut& mine = doGetShortcut();
            const KeyboardShortcut& theirs = entry->doGetShortcut();
            
            return mine.hasKey() && theirs.hasKey() && mine == theirs;
        }

        int KeyboardShortcutEntry::actionContext() const {
            return doGetActionContext();
        }

        wxString KeyboardShortcutEntry::actionContextDescription() const {
            return actionContextName(actionContext());
        }

        wxString KeyboardShortcutEntry::actionDescription() const {
            return doGetActionDescription();
        }
        
        wxString KeyboardShortcutEntry::shortcutDescription() const {
            return doGetShortcut().shortcutDisplayString();
        }
        
        bool KeyboardShortcutEntry::equals(const KeyboardShortcut& shortcut) const {
            return doGetShortcut() == shortcut;
        }
        
        void KeyboardShortcutEntry::updateShortcut(const KeyboardShortcut& shortcut) {
            doUpdateShortcut(shortcut);
        }
    }
}
