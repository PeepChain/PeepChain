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
 * When there is one, the editor shows a popup with its release notes (the release's description on GitHub),
 * once per DAW session.
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

    struct Release {
        juce::String tag, title, notes, url;             // notes: the release's description (GitHub Markdown)
    };

    State getState() const { return state.load(); }
    /** The newer version's tag, once getState() is available. */
    juce::String getLatestVersion() const { const juce::ScopedLock l(lock); return latest.tag; }
    /** Everything about the newer release, once getState() is available. */
    Release getLatest() const { const juce::ScopedLock l(lock); return latest; }
    /** Its page on GitHub (the releases page when GitHub didn't say). */
    juce::URL latestPage() const {
        const juce::ScopedLock l(lock);
        return latest.url.startsWith("https://github.com/") ? juce::URL(latest.url) : releasesPage();
    }
    /** True the first time it's asked in this process: the editor that gets it shows the popup on its own,
     *  the others leave it to the header's notice. */
    bool claimPopup() { return !popupClaimed.exchange(true); }
    /** Tests: as if GitHub had answered with this release. */
    void pretend(const Release& r) {
        stopThread(6000);
        { const juce::ScopedLock l(lock); latest = r; }
        state = isNewer(r.tag, currentVersion()) ? State::available : State::upToDate;
    }

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
        return fetchLatest(ownerRepo, std::move(keepGoing)).tag;
    }

    /** The latest release of "<owner>/<repo>" (an empty tag on any failure). Blocks for up to ~5 s. */
    static Release fetchLatest(const juce::String& ownerRepo, std::function<bool()> keepGoing = [] { return true; }) {
        const juce::URL api("https://api.github.com/repos/" + ownerRepo + "/releases/latest");
        int status = 0;
        const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                 .withConnectionTimeoutMs(5000)
                                 .withExtraHeaders("Accept: application/vnd.github+json\r\nUser-Agent: PeepChain-update-check")
                                 .withStatusCode(&status)
                                 .withProgressCallback([keepGoing](int, int) { return keepGoing(); });
        auto stream = api.createInputStream(options);
        if (stream == nullptr || status != 200) return {};
        const auto json = juce::JSON::parse(stream->readEntireStreamAsString());
        Release r;
        r.tag = json.getProperty("tag_name", {}).toString();
        r.title = json.getProperty("name", {}).toString();
        r.notes = json.getProperty("body", {}).toString();
        r.url = json.getProperty("html_url", {}).toString();
        return r;
    }

    /** GitHub's "Full Changelog: <comparison link>" line, left out of the popup's notes. */
    static bool isChangelogLine(const juce::String& line) { return line.removeCharacters("*_#").trim().startsWithIgnoreCase("Full Changelog"); }

    /** GitHub Markdown as plain text for the popup: headings and emphasis marks dropped, links reduced to their
     *  text, list items given bullets, blank-line runs collapsed. */
    static juce::String plainNotes(const juce::String& markdown) {
        juce::StringArray out;
        for (auto line : juce::StringArray::fromLines(markdown)) {
            line = line.trimEnd();
            if (line.startsWith("<!--") || line.trim() == "---" || isChangelogLine(line)) continue;
            const auto trimmed = line.trimStart();
            const int indent = line.length() - trimmed.length();
            line = trimmed;
            bool heading = false;
            if (line.startsWith("#")) { line = line.trimCharactersAtStart("#").trimStart(); heading = true; }
            if (line.startsWith("- ") || line.startsWith("* ") || line.startsWith("+ "))
                line = juce::String::repeatedString(" ", juce::jmin(indent, 8)) + juce::String::fromUTF8("\xe2\x80\xa2  ") + line.substring(2).trimStart();
            else if (indent > 0 && line.isNotEmpty())                 // a list item's next line: under its text
                line = juce::String::repeatedString(" ", juce::jmin(indent, 8) + 2) + line;
            // [text](url) -> text, ![alt](url) -> nothing
            for (int from = 0;;) {
                const int open = line.indexOf(from, "[");
                const int close = open >= 0 ? line.indexOf(open, "](") : -1;
                const int end = close >= 0 ? line.indexOf(close, ")") : -1;
                if (end < 0) break;
                const bool image = open > 0 && line[open - 1] == '!';
                const int start = image ? open - 1 : open;
                const auto text = image ? juce::String() : line.substring(open + 1, close);
                line = line.substring(0, start) + text + line.substring(end + 1);
                from = start + text.length();
            }
            for (auto* mark : {"**", "__", "`"}) line = line.replace(mark, "");
            if (heading) line = line.toUpperCase();
            if (line.isEmpty() && (out.isEmpty() || out[out.size() - 1].isEmpty())) continue;
            if (heading && !out.isEmpty() && out[out.size() - 1].isNotEmpty()) out.add({});
            out.add(line);
        }
        while (!out.isEmpty() && out[out.size() - 1].isEmpty()) out.remove(out.size() - 1);
        return out.joinIntoString("\n");
    }

private:
    void run() override {
        const auto release = fetchLatest(repo(), [this] { return ! threadShouldExit(); });
        if (release.tag.isEmpty() || threadShouldExit()) { state = State::failed; return; }
        { const juce::ScopedLock l(lock); latest = release; }
        state = isNewer(release.tag, currentVersion()) ? State::available : State::upToDate;
    }

    std::atomic<State> state{State::idle};
    juce::CriticalSection lock;
    Release latest;
    std::atomic<bool> popupClaimed{false};
};
