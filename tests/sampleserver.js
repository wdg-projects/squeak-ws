import { WebSocketServer } from 'ws';

const wss = new WebSocketServer({ port: 65500 });

wss.on("connection", ws => {
    ws.on("message", data => {
        ws.send(data);
    });
    ws.send("start");
});
