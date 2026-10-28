// runs inside the page: youtube only answers timedtext requests that carry the player's anti-bot token, so the player
// fetches the track itself and this reuses the url it just used

let handled = "";

function currentVideo() {
  return new URLSearchParams(location.search).get("v") || "";
}

// the lyrics are the captions written in the language being sung: a hand-made track in the same language as the
// auto-generated one, otherwise the auto-generated one itself. without an auto track nobody says which language is
// sung, so the pick is only a guess and the game checks it against the lyrics it found before trusting it
function pick(captions) {
  const tracks = captions.captionTracks || [];
  const spoken = tracks.find((track) => track.kind === "asr");
  const language = (code) => (code || "").split("-")[0];
  const manual = tracks.filter((track) => track.kind !== "asr");
  if (spoken) {
    const same = manual.find((track) => language(track.languageCode) === language(spoken.languageCode));
    return { track: same || spoken, sure: !!same };
  }
  const audio = (captions.audioTracks || [])[captions.defaultAudioTrackIndex || 0];
  const track = tracks[audio ? audio.defaultCaptionTrackIndex || 0 : 0];
  return track ? { track, sure: false } : null;
}

function requested(video, track) {
  const asr = track.kind === "asr";
  return performance
    .getEntriesByType("resource")
    .map((entry) => entry.name)
    .reverse()
    .find(
      (name) =>
        name.includes("/api/timedtext") &&
        name.includes("v=" + video) &&
        name.includes("lang=" + encodeURIComponent(track.languageCode)) &&
        name.includes("pot=") &&
        name.includes("kind=asr") === asr
    );
}

function clean(text) {
  return text.replace(/\n/g, " ").replace(/[♪♫]/g, "").replace(/\s+/g, " ").trim();
}

function wordCount(text) {
  return text ? text.split(" ").length : 0;
}

// json3: events hold the timed lines, their segs hold the words with offsets inside the event. marks say which word
// starts when, so the game can show each word as it's said
function parse(json) {
  const lines = [];
  const words = [];
  const events = (json.events || []).filter((event) => event.segs && !event.aAppend);
  events.forEach((event, index) => {
    let raw = "";
    const marks = [];
    for (const seg of event.segs) {
      const piece = (seg.utf8 || "").replace(/\n/g, " ").replace(/[♪♫]/g, "");
      const ms = event.tStartMs + (seg.tOffsetMs || 0);
      const pieceWords = wordCount(clean(piece));
      if (pieceWords > 0) {
        // a piece glued to the previous one continues its last word, the count works out the same either way
        marks.push({ word: wordCount(clean(raw + piece)) - pieceWords, ms });
        clean(piece).split(" ").forEach((word) => words.push({ ms, text: word }));
      }
      raw += piece;
    }
    const text = clean(raw);
    if (!text || /^[\[(].*[\])]$/.test(text)) {
      return;
    }
    lines.push({ ms: event.tStartMs, text, marks: marks.length > 1 ? marks : [] });
    // a gap before the next line clears the text instead of leaving the last one hanging through the instrumental
    const end = event.tStartMs + (event.dDurationMs || 0);
    const next = events[index + 1];
    if (event.dDurationMs && (!next || next.tStartMs - end > 1500)) {
      lines.push({ ms: end, text: "" });
    }
  });
  return { lines, words };
}

async function grab() {
  const player = document.getElementById("movie_player");
  const video = currentVideo();
  const response = player && player.getPlayerResponse && player.getPlayerResponse();
  if (!video || video === handled || !response || !response.videoDetails || response.videoDetails.videoId !== video) {
    return;
  }
  handled = video;
  const captions = response.captions && response.captions.playerCaptionsTracklistRenderer;
  const picked = captions && pick(captions);
  if (!picked) {
    return;
  }
  const track = picked.track;
  let url = requested(video, track);
  if (!url) {
    // switch the track on just long enough for the player to fetch it, hidden, then put things back how they were
    const before = player.getOption("captions", "track");
    const hide = document.createElement("style");
    hide.textContent = ".ytp-caption-window-container { visibility: hidden !important; }";
    document.head.appendChild(hide);
    player.loadModule("captions");
    player.setOption("captions", "track", { languageCode: track.languageCode, kind: track.kind || "" });
    for (let waited = 0; !url && waited < 8000; waited += 250) {
      await new Promise((resolve) => setTimeout(resolve, 250));
      url = requested(video, track);
    }
    if (before && before.languageCode) {
      player.setOption("captions", "track", before);
    } else {
      player.unloadModule("captions");
    }
    hide.remove();
  }
  if (!url || currentVideo() !== video) {
    return;
  }
  try {
    const parsed = parse(await (await fetch(url)).json());
    window.postMessage({ lyricsCaptions: { videoId: video, trusted: picked.sure, ...parsed } }, location.origin);
  } catch (error) {}
}

setInterval(grab, 1000);
