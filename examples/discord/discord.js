var tries = 0;
var socket = {};
var oauth_token = "207646673902501888";

function send(cmd, args, nonce) {
    if (!args) {
        args = {};
    }
    if (!nonce) {
        nonce = cmd;
    }
    socket.send(JSON.stringify({
        "cmd": cmd,
        "args": args,
        "nonce": nonce
    }));
}

function subscribe(event, args) {
    if (!args) {
        args = {};
    }
    socket.send(JSON.stringify({
        "cmd": "SUBSCRIBE",
        "args": args,
        "evt": event,
        "nonce": event
    }));
}

function load_channel(guild, channel) {
    var newpath = "/overlay/voice/" + guild + "/" + channel;
    if (window.location.pathname != newpath) {
        window.location.pathname = newpath;
    }
}

function open_socket() {
    socket = new WebSocket("ws://127.0.0.1:" + (6463 + (tries++ % 10)) + "/?v=1&client_id=" + oauth_token);

    socket.onopen = function(event) {
        console.log("connected");
    };

    socket.onerror = function(event) {
        try {
            socket.close();
        } catch {}
        setTimeout(open_socket, 250);
    };

    socket.onmessage = function(event) {
        var j = JSON.parse(event.data);
        console.log(j);

        if (j.cmd == "DISPATCH") {
            if (j.evt == "READY") {
                send("AUTHORIZE", {
                    "client_id": oauth_token,
                    "scopes": ["rpc", "messages.read"],
                    "prompt": "none",
                });
                return;
            }
            if (j.evt == "VOICE_CHANNEL_SELECT") {
                if (j.data && j.data.guild_id) {
                    load_channel(j.data.guild_id, j.data.channel_id);
                }
            }
        } else if (j.cmd == "AUTHENTICATE") {
            if (j.evt == "ERROR") {
                console.log("Auth error");
                return;
            } else {
                subscribe("VOICE_CHANNEL_SELECT");
                send("GET_SELECTED_VOICE_CHANNEL");
                return;
            }
        } else if (j.cmd == "AUTHORIZE") {
            var xhttp = new XMLHttpRequest();
            xhttp.onreadystatechange = function() {
                if (this.readyState == 4 && this.status == 200) {
                    var j = JSON.parse(this.responseText);
                    send("AUTHENTICATE", { "access_token" : j.access_token });
                }
            };
            xhttp.open("POST", "https://streamkit.discord.com/overlay/token");
            xhttp.send(JSON.stringify({
                "code": j.data.code
            }));
            return;
        } else if (j.cmd == "GET_SELECTED_VOICE_CHANNEL") {
            if (j.data && j.data.guild_id) {
                load_channel(j.data.guild_id, j.data.id);
            }
        }
    };
}

open_socket();
