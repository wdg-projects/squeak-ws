import { WebSocketServer } from 'ws';
import { StringDecoder } from 'node:string_decoder';

const wss = new WebSocketServer({ port: 65500 });

wss.on("connection", ws => {
    ws.on("message", data => {
        const decoder = new StringDecoder('utf8');
        let text = decoder.write(data);
        if (text === "@terminate") {
            setTimeout(() => ws.close(), 500);

        } else {
            ws.send(data);
        }
    });
    ws.send("start");
});
