#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

// Set by CMake (PEEPCHAIN_GITHUB_REPO, PROJECT_VERSION); these fallbacks only matter outside the CMake build.
#ifndef PEEPCHAIN_VERSION
 #define PEEPCHAIN_VERSION "0.0.0"
#endif
#ifndef PEEPCHAIN_GITHUB_REPO
 #define PEEPCHAIN_GITHUB_REPO "PeepChain/PeepChain"
#endif

/**
 * Asks GitHub once, in the background, whether a newer release of the plugin exists:
 * GET https://api.github.com/repos/<owner>/<repo>/releases/latest and compares its tag (e.g. "v0.2.0") with
 * this build's version. Nothing is sent but that request. It never blocks the editor or the audio, gives up
 * after a few seconds, and stays quiet on any failure (offline, rate-limited, no release yet). If the repo is
 * set to something without an owner (e.g. -DPEEPCHAIN_GITHUB_REPO="") it does not ask at all.
 *
 * Held by a juce::SharedResourcePointer, so every open editor in the process shares one check.
 */
class UpdateChecker : private juce::Thread {
public:
    enum class State { idle, checking, upToDate, available, failed };

    UpdateChecker() : juce::Thread("PeepChain update check") {
        if (isConfigured()) { state = State::checking; startThread(juce::Thread::Priority::low); }
    }
    ~UpdateChecker() override { stopThread(6000); }

    static juce::String currentVersion() { return PEEPCHAIN_VERSION; }
    static juce::String repo() { return PEEPCHAIN_GITHUB_REPO; }
    static bool isConfigured() { return repo().contains("/"); }
    static juce::URL releasesPage() { return juce::URL("https://github.com/" + repo() + "/releases/latest"); }
    static juce::URL repoPage() { return juce::URL("https://github.com/" + repo()); }

    State getState() const { return state.load(); }
    /** The newer version's tag, once getState() is available. */
    juce::String getLatestVersion() const { const juce::ScopedLock l(lock); return latest; }

    /** "v1.2.10" > "v1.2.9"; missing parts count as 0; anything after a '-' (pre-release) is ignored. */
    static bool isNewer(const juce::String& candidate, const juce::String& current) {
        auto parts = [](juce::String v) {
            v = v.trim().trimCharactersAtStart("vV").upToFirstOccurrenceOf("-", false, false);
            juce::Array<int> p; for (auto& t : juce::StringArray::fromTokens(v, ".", "")) p.add(t.getIntValue());
            while (p.size() < 3) p.add(0);
            return p;
        };
        const auto a = parts(candidate), b = parts(current);
        for (int i = 0; i < juce::jmax(a.size(), b.size()); ++i) {
            const int x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
            if (x != y) return x > y;
        }
        return false;
    }

    /** The latest release's tag for "<owner>/<repo>", or empty on any failure. Blocks for up to ~5 s. */
    static juce::String fetchLatestTag(const juce::String& ownerRepo, std::function<bool()> keepGoing = [] { return true; }) {
        const juce::URL api("https://api.github.com/repos/" + ownerRepo + "/releases/latest");
        int status = 0;
        const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                 .withConnectionTimeoutMs(5000)
                                 .withExtraHeaders("Accept: application/vnd.github+json\r\nUser-Agent: PeepChain-update-check")
                                 .withStatusCode(&status)
                                 .withProgressCallback([keepGoing](int, int) { return keepGoing(); });
        auto stream = api.createInputStream(options);
        if (stream == nullptr || status != 200) return {};
        return juce::JSON::parse(stream->readEntireStreamAsString()).getProperty("tag_name", {}).toString();
    }

private:
    void run() override {
        const auto tag = fetchLatestTag(repo(), [this] { return ! threadShouldExit(); });
        if (tag.isEmpty() || threadShouldExit()) { state = State::failed; return; }
        { const juce::ScopedLock l(lock); latest = tag; }
        state = isNewer(tag, currentVersion()) ? State::available : State::upToDate;
    }

    std::atomic<State> state{State::idle};
    juce::CriticalSection lock;
    juce::String latest;
};
