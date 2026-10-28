#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lyrics::music {

struct Track {
    std::string title;
    std::string artist;
    std::string album;
    std::string cover;
    int duration = 0;
};

struct Line {
    long long ms = 0;
    std::string text;
    // when the words start, from the video's captions: the index of a word in text (split on spaces) and its time
    struct Mark {
        int word = 0;
        long long ms = 0;
    };
    std::vector<Mark> marks;
};

// everything below blocks on the network: call from a worker thread
// Deezer's public search, no account needed
[[nodiscard]] std::vector<Track> search(std::string_view query);
// the first video YouTube's own results page lists for the track
[[nodiscard]] std::optional<std::string> youtube_video(const Track& track);
// LRCLIB synced lyrics, from the disk cache when this track was fetched before; empty when there are none
[[nodiscard]] std::vector<Line> fetch_lyrics(const Track& track);
[[nodiscard]] std::optional<std::string> download(const std::string& url);

[[nodiscard]] std::vector<Line> parse_lrc(std::string_view lrc);
// a YouTube video title cut down to "artist - title": no (Official Video), [Clip], feat. tails
[[nodiscard]] Track from_video_title(std::string_view title, std::string_view channel);

}
