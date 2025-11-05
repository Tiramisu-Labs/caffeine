
const s = JSON.stringify({status: "success", message: "Caffeine handler executed successfully! Instance: integration_test (Method: GET)"}, 0, 2);

const ret = () => {
    console.log("HTTP/1.1 200 OK");
    console.log("Content-Type: application/json");
    console.log(`Content-Length: ${s.length}`);
    console.log("Connection: close");
    console.log("");
    console.log(s);

    process.stderr.write("test error");
}

ret();