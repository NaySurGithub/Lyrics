// the page itself can't reach 127.0.0.1 (cors and private network rules), the extension's worker can
const bridge = "http://127.0.0.1:8974";

chrome.runtime.onMessage.addListener((message, sender) => {
  if (message.kind === "poll") {
    // a song picked in game plays in the tab that asked, without bringing chrome to the front
    fetch(bridge + "/poll")
      .then((response) => response.json())
      .then((answer) => {
        if (answer.open && sender.tab) {
          chrome.tabs.update(sender.tab.id, { url: answer.open });
        }
      })
      .catch(() => {});
    return;
  }
  fetch(bridge + (message.kind === "captions" ? "/captions" : "/update"), {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(message),
  }).catch(() => {});
});
