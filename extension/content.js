// reports the playing video once a second, and right away on play, pause and seeks
function videoId() {
  const params = new URLSearchParams(location.search);
  return params.get("v") || "";
}

function title() {
  const heading = document.querySelector("h1.ytd-watch-metadata yt-formatted-string, ytmusic-player-bar .title");
  return (heading && heading.textContent.trim()) || document.title.replace(/ - YouTube( Music)?$/, "");
}

function channel() {
  const owner = document.querySelector("#owner ytd-channel-name a, ytmusic-player-bar .byline a");
  return owner ? owner.textContent.trim() : "";
}

// only a playing tab reports on its own; play, pause and seeks always do, so a pause still gets through once
function report(event) {
  const video = document.querySelector("video");
  if (!video || !videoId() || (!event && (video.paused || video.ended))) {
    return;
  }
  const ad = !!document.querySelector("#movie_player.ad-showing");
  chrome.runtime.sendMessage({
    videoId: videoId(),
    title: title(),
    artist: channel(),
    // during an ad the <video> is the ad, so the song is held at its start until it really plays
    positionMs: ad ? 0 : Math.round(video.currentTime * 1000),
    durationMs: ad ? 0 : Math.round((video.duration || 0) * 1000),
    isPlaying: !ad && !video.paused && !video.ended,
  });
}

setInterval(report, 1000);
setInterval(() => chrome.runtime.sendMessage({ kind: "poll" }), 1000);
for (const event of ["play", "pause", "seeked", "loadedmetadata"]) {
  document.addEventListener(event, report, true);
}

// captions.js lives in the page and can't reach the extension, it hands its captions over here
window.addEventListener("message", (event) => {
  if (event.source === window && event.data && event.data.lyricsCaptions) {
    chrome.runtime.sendMessage({ kind: "captions", ...event.data.lyricsCaptions });
  }
});
