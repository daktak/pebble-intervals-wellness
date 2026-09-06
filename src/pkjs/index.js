var Clay = require("@rebble/clay");
var clayConfig = require("./config.json");
var clay = new Clay(clayConfig);
var base64 = require("base-64");

function getSettings() {
  try {
    var raw = localStorage.getItem("clay-settings");
    if (raw) return JSON.parse(raw);
  } catch (e) {}
  return {};
}

function effApiKey() {
  var s = getSettings();
  if (s.API_KEY && s.API_KEY !== "") return s.API_KEY;
  return localStorage.getItem("icu_api_key") || "";
}

function sendStatus(msg) {
  var s = String(msg).slice(0, 60);
  console.log("sendStatus " + s);
  Pebble.sendAppMessage({ STATUS: s }, function() { console.log("status ok"); }, function(e) { console.log("status fail " + e); });
}

function pushWellness(records) {
  var key = effApiKey();
  var aid = "0";
  if (!key) {
    sendStatus("ERR no API key");
    return;
  }
  var url = "https://intervals.icu/api/v1/athlete/" + aid + "/wellness-bulk";
  console.log("PUT wellness-bulk " + url + " n=" + records.length + " " + JSON.stringify(records).slice(0, 300));
  var xhr = new XMLHttpRequest();
  xhr.open("PUT", url, true);
  try { xhr.setRequestHeader("Content-Type", "application/json"); } catch (e) {}
  try { xhr.setRequestHeader("Authorization", "Basic " + base64.encode("API_KEY:" + key)); } catch (e) { console.log("auth err " + e); }
  xhr.onload = function() {
    console.log("wellness resp " + xhr.status + " " + xhr.responseText.slice(0, 500));
    if (xhr.status >= 200 && xhr.status < 300) {
      var label = records.length === 1 ? records[0].id : records[0].id + "+" + records[records.length - 1].id;
      sendStatus("OK " + label);
    } else {
      var err = "ERR " + xhr.status;
      try {
        var j = JSON.parse(xhr.responseText);
        if (j.error) err = "ERR " + String(j.error).slice(0, 30);
        else if (j.message) err = "ERR " + String(j.message).slice(0, 30);
      } catch (e2) {}
      if (xhr.status === 401) err = "ERR bad key";
      sendStatus(err);
    }
  };
  xhr.onerror = function() { console.log("wellness net err"); sendStatus("ERR net"); };
  xhr.send(JSON.stringify(records));
}

Pebble.addEventListener("ready", function() {
  console.log("JS ready wellness " + localStorage.getItem("clay-settings"));
  try {
    var s = getSettings();
    var h = parseInt(s.SYNC_HOUR, 10);
    var m = parseInt(s.SYNC_MINUTE, 10);
    if (!isNaN(h) && !isNaN(m)) {
      console.log("pushing sync time " + h + ":" + m);
      Pebble.sendAppMessage({ SYNC_HOUR: h, SYNC_MINUTE: m }, function() { console.log("sync time push ok"); }, function(e) { console.log("sync time push fail " + e); });
    }
    var k = s.API_KEY;
    if (k) localStorage.setItem("icu_api_key", k);
  } catch (e) { console.log("ready push err " + e); }
});

Pebble.addEventListener("appmessage", function(e) {
  console.log("appmessage " + JSON.stringify(e.payload));
  var p = e.payload;
  if (typeof p.API_KEY !== "undefined") localStorage.setItem("icu_api_key", p.API_KEY);
  if (typeof p.SYNC_HOUR !== "undefined") localStorage.setItem("sync_hour", String(p.SYNC_HOUR));
  if (typeof p.SYNC_MINUTE !== "undefined") localStorage.setItem("sync_minute", String(p.SYNC_MINUTE));
  var yDate = p.Y_DATE;
  var tDate = p.T_DATE;
  var hasY = typeof yDate !== "undefined" && yDate;
  var hasT = typeof tDate !== "undefined" && tDate;
  if (hasY || hasT) {
    var records = [];
    if (hasY) {
      var r = { id: yDate };
      if (typeof p.Y_STEPS !== "undefined") r.steps = parseInt(p.Y_STEPS, 10);
      if (typeof p.Y_SLEEP !== "undefined") r.sleepSecs = parseInt(p.Y_SLEEP, 10);
      if (typeof p.Y_RHR !== "undefined" && parseInt(p.Y_RHR, 10) > 0) r.restingHR = parseInt(p.Y_RHR, 10);
      if (typeof p.Y_SHR !== "undefined" && parseInt(p.Y_SHR, 10) > 0) r.avgSleepingHR = parseInt(p.Y_SHR, 10);
      records.push(r);
    }
    if (hasT) {
      var r2 = { id: tDate };
      if (typeof p.T_STEPS !== "undefined") r2.steps = parseInt(p.T_STEPS, 10);
      if (typeof p.T_SLEEP !== "undefined") r2.sleepSecs = parseInt(p.T_SLEEP, 10);
      if (typeof p.T_RHR !== "undefined" && parseInt(p.T_RHR, 10) > 0) r2.restingHR = parseInt(p.T_RHR, 10);
      if (typeof p.T_SHR !== "undefined" && parseInt(p.T_SHR, 10) > 0) r2.avgSleepingHR = parseInt(p.T_SHR, 10);
      records.push(r2);
    }
    if (records.length === 0) sendStatus("ERR no data");
    else pushWellness(records);
    return;
  }
  if (typeof p.CMD !== "undefined") {
    if (p.CMD === 1) sendStatus("READY");
  }
});
