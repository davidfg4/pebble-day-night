Pebble.addEventListener("ready",
  function(e) {
    var time = Math.round((new Date()).getTime() / 1000);
    Pebble.sendAppMessage({"0": time});
  }
);
