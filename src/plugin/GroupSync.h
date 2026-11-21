#pragma once
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <vector>

/**
 * Links PeepChain instances into named groups: every instance in a group shares one sound. Change an effect, the
 * order, a threshold or a send on one track and the others in its group follow.
 *
 * There are none to begin with. A group is created from one instance, with that instance's sound and a name
 * ("Group 1", "Group 2"... which can be changed later); other instances join it and take on its sound. A group
 * lasts as long as something is in it, or until it's deleted (its tracks keep the sound they have). Each instance saves its group's id and name with the session, so reopening the session
 * puts the same tracks back together.
 *
 * One hub per process (held by a juce::SharedResourcePointer in every instance), so it links the instances a
 * DAW runs in one process: all of them in most hosts. Instances a host runs in separate processes (a sandbox,
 * a bridge) can't see each other, and neither can an AU and a VST3 copy of the plugin.
 *
 * How it stays in step: a member marks itself `changed` whenever one of its settings moves (from any thread,
 * the audio thread's automation included). Twenty times a second, on the message thread, the hub reads each
 * changed member's sound and, if it differs from what the group last agreed on, makes that the group's sound
 * and hands it to the other members. Members that are handed a sound end up equal to the group's, so their own
 * `changed` turns into a no-op: nothing bounces back and forth.
 */
class GroupHub : private juce::Timer {
public:
    struct Member {
        virtual ~Member() = default;
        /** What the group shares, as normalised parameter values and the order (message thread). */
        virtual juce::ValueTree groupSound() const = 0;
        /** Take on the group's sound (message thread). */
        virtual void applyGroupSound(const juce::ValueTree&) = 0;
        /** The host's name for the track, or empty when it doesn't say. */
        virtual juce::String groupMemberName() const = 0;
        /** The group it's in was renamed, joined or left (message thread): what it saves has changed. */
        virtual void groupDetailsChanged() {}
        /** Set by the member when what it shares may have changed (any thread). */
        void markGroupChanged() noexcept { changed.store(true, std::memory_order_relaxed); }
    private:
        friend class GroupHub;
        juce::String groupId;                            // empty: in no group (guarded by the hub's lock)
        std::atomic<bool> changed{false};
    };

    struct Info {
        juce::String id, name;
        juce::StringArray members;                       // by track name ("Untitled" where the host gives none)
    };

    GroupHub() { startTimerHz(20); }
    ~GroupHub() override { stopTimer(); }

    void add(Member* m) { const juce::ScopedLock l(lock); members.addIfNotAlreadyThere(m); }
    void remove(Member* m) {
        const juce::ScopedLock l(lock);
        members.removeFirstMatchingValue(m);
        forgetIfEmpty(m->groupId);
    }

    /** Makes a new group with this member's sound, and puts it in (out of any it was in). Its id. Message thread. */
    juce::String create(Member& m, const juce::String& name) {
        JUCE_ASSERT_MESSAGE_THREAD
        const juce::ScopedLock l(lock);
        const auto id = juce::Uuid().toString();
        groups[id] = {cleanName(name), m.groupSound()};
        moveTo(m, id);
        return id;
    }
    /** Puts a member in an existing group, where it takes on the group's sound. False if there's no such group. */
    bool join(Member& m, const juce::String& id) {
        JUCE_ASSERT_MESSAGE_THREAD
        const juce::ScopedLock l(lock);
        const auto it = groups.find(id);
        if (it == groups.end()) return false;
        if (m.groupId == id) return true;
        const auto sound = it->second.sound;
        moveTo(m, id);
        m.applyGroupSound(sound);
        m.changed = false;
        return true;
    }
    /** For a loaded session: joins the group with this id, or (the first of its tracks to load) makes it again,
     *  with this member's sound and the saved name. */
    void rejoin(Member& m, const juce::String& id, const juce::String& name) {
        JUCE_ASSERT_MESSAGE_THREAD
        if (id.isEmpty()) { leave(m); return; }
        const juce::ScopedLock l(lock);
        if (groups.count(id) == 0) { groups[id] = {cleanName(name), m.groupSound()}; moveTo(m, id); return; }
        join(m, id);
    }
    void leave(Member& m) {
        JUCE_ASSERT_MESSAGE_THREAD
        const juce::ScopedLock l(lock);
        if (m.groupId.isNotEmpty()) moveTo(m, {});
    }
    /** Deletes a group: every track in it leaves (keeping the sound it has). */
    void dissolve(const juce::String& id) {
        JUCE_ASSERT_MESSAGE_THREAD
        const juce::ScopedLock l(lock);
        const auto snapshot = members;
        for (auto* m : snapshot) if (members.contains(m) && m->groupId == id) moveTo(*m, {});
        groups.erase(id);
    }
    void rename(const juce::String& id, const juce::String& name) {
        JUCE_ASSERT_MESSAGE_THREAD
        const juce::ScopedLock l(lock);
        const auto it = groups.find(id);
        if (it == groups.end() || it->second.name == cleanName(name)) return;
        it->second.name = cleanName(name);
        for (auto* m : members) if (m->groupId == id) m->groupDetailsChanged();
    }

    juce::String groupOf(const Member& m) const { const juce::ScopedLock l(lock); return m.groupId; }
    juce::String nameOf(const juce::String& id) const {
        const juce::ScopedLock l(lock);
        const auto it = groups.find(id);
        return it != groups.end() ? it->second.name : juce::String();
    }
    int memberCount(const juce::String& id) const {
        const juce::ScopedLock l(lock);
        int n = 0;
        for (auto* m : members) n += id.isNotEmpty() && m->groupId == id ? 1 : 0;
        return n;
    }
    /** Every group there is, by name. */
    std::vector<Info> list() const {
        const juce::ScopedLock l(lock);
        std::vector<Info> out;
        for (const auto& [id, g] : groups) {
            Info i{id, g.name, {}};
            for (auto* m : members)
                if (m->groupId == id) { const auto n = m->groupMemberName().trim(); i.members.add(n.isNotEmpty() ? n : juce::String("Untitled")); }
            out.push_back(i);
        }
        std::sort(out.begin(), out.end(), [](const Info& a, const Info& b) { return a.name.compareNatural(b.name) < 0; });
        return out;
    }
    /** The name a new group gets: "Group <n>", n being the number of groups there will be (counting on from
     *  there if another group already has that name). */
    juce::String suggestName() const {
        const juce::ScopedLock l(lock);
        for (int n = (int)groups.size() + 1;; ++n) {
            const auto name = "Group " + juce::String(n);
            bool used = false;
            for (const auto& [id, g] : groups) used = used || g.name.equalsIgnoreCase(name);
            if (!used) return name;
        }
    }
    static juce::String cleanName(const juce::String& name) {
        auto n = name.replaceCharacters("\r\n\t", "   ").trim().substring(0, kMaxNameLength).trim();
        return n.isNotEmpty() ? n : juce::String("Group");
    }
    static constexpr int kMaxNameLength = 40;

    /** Runs a round of syncing now (tests; the timer does this on its own). */
    void syncNow() { timerCallback(); }

    /** True when two sounds are the same: the same properties, numbers equal to within a hair (a parameter
     *  can hand back its value a rounding error away from what it was given). */
    static bool sameSound(const juce::ValueTree& a, const juce::ValueTree& b) {
        if (a.isValid() != b.isValid() || a.getNumProperties() != b.getNumProperties()) return false;
        for (int i = 0; i < a.getNumProperties(); ++i) {
            const auto name = a.getPropertyName(i);
            if (!b.hasProperty(name)) return false;
            const auto& x = a.getProperty(name); const auto& y = b.getProperty(name);
            if (x.isString() || y.isString()) { if (x.toString() != y.toString()) return false; }
            else if (std::abs((double)x - (double)y) > 1e-6) return false;
        }
        return true;
    }

private:
    struct Group { juce::String name; juce::ValueTree sound; };   // sound: as last agreed

    void timerCallback() override {
        const juce::ScopedLock l(lock);
        const auto snapshot = members;                   // a host could add or remove an instance while we call out
        for (auto* m : snapshot) {
            if (!members.contains(m) || m->groupId.isEmpty() || !m->changed.exchange(false)) continue;
            const auto id = m->groupId;
            const auto it = groups.find(id);
            if (it == groups.end()) continue;
            const auto sound = m->groupSound();
            if (sameSound(sound, it->second.sound)) continue;
            it->second.sound = sound;
            for (auto* other : snapshot)
                if (other != m && members.contains(other) && other->groupId == id) other->applyGroupSound(sound);
        }
    }
    void moveTo(Member& m, const juce::String& id) {     // (lock held)
        const auto was = std::exchange(m.groupId, id);
        forgetIfEmpty(was);
        m.changed = false;
        m.groupDetailsChanged();
    }
    void forgetIfEmpty(const juce::String& id) {          // a group lasts as long as something is in it (lock held)
        if (id.isEmpty()) return;
        for (auto* m : members) if (m->groupId == id) return;
        groups.erase(id);
    }

    juce::CriticalSection lock;                          // members can come and go on any thread
    juce::Array<Member*> members;
    std::map<juce::String, Group> groups;
    JUCE_DECLARE_NON_COPYABLE(GroupHub)
};
