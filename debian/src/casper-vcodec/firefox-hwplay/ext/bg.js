// Hand video pages to the MT6765 hardware H.264 player (hwplay) through the native host "hwplay".
let auto = true;                                   // intercept YouTube watch pages automatically
browser.storage.local.get("auto").then(r => { if (r.auto === false) auto = false; setMenu(); });

function play(url) { return browser.runtime.sendNativeMessage("hwplay", {url}); }

browser.webRequest.onBeforeRequest.addListener(
  d => {
    if (!auto || d.method !== "GET") return {};
    play(d.url).catch(() => {});
    // the blocked load leaves an error page: step back, or close a tab that was opened just for the video
    setTimeout(() => browser.tabs.goBack(d.tabId).catch(() => browser.tabs.remove(d.tabId).catch(() => {})), 400);
    return {cancel: true};                        // keep Firefox from also loading (and playing) the page
  },
  {urls: ["*://www.youtube.com/watch*", "*://m.youtube.com/watch*", "*://youtu.be/*", "*://www.youtube.com/shorts/*"],
   types: ["main_frame"]},
  ["blocking"]);

// YouTube is a single-page app: clicking a video from the home page only changes the address (pushState), no page load,
// so webRequest never sees it. Catch those navigations too, hand the video over and stop/step back the in-page player.
let last = {url: "", t: 0};
browser.webNavigation.onHistoryStateUpdated.addListener(d => {
  if (!auto || d.frameId !== 0) return;
  if (!/^https:\/\/(www|m)\.youtube\.com\/(watch|shorts\/)/.test(d.url)) return;
  if (d.url === last.url && Date.now() - last.t < 8000) return;
  last = {url: d.url, t: Date.now()};
  play(d.url).catch(() => {});
  browser.tabs.executeScript(d.tabId, {code: "document.querySelectorAll('video').forEach(v => v.pause()); history.back();"}).catch(() => {});
}, {url: [{hostSuffix: "youtube.com"}]});

browser.browserAction.onClicked.addListener(tab => play(tab.url));

function setMenu() {
  browser.contextMenus.removeAll().then(() => {
    browser.contextMenus.create({id: "link", title: "Play link in hardware player", contexts: ["link"]});
    browser.contextMenus.create({id: "auto", type: "checkbox", checked: auto,
      title: "Open YouTube videos in the hardware player", contexts: ["browser_action", "page"]});
  });
}
browser.contextMenus.onClicked.addListener(info => {
  if (info.menuItemId === "link") play(info.linkUrl);
  if (info.menuItemId === "auto") { auto = info.checked; browser.storage.local.set({auto}); }
});
