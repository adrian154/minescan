const fs = require("fs");

const array = fs.readFileSync("exclude.txt", "utf-8")
    .split("\n")
    .map(line => line.trim())
    .filter(line => line.length > 0 && line[0] != '#')
    .map(line => {
        const [addr, bits] = line.split("/");
        const octets = addr.split(".").map(Number);
        return [
            ((octets[0]<<24) + (octets[1]<<16) + (octets[2]<<8) + octets[3])>>>0,
            ~(0xffffffff >>> bits) >>> 0
        ];
    })
    .map(pair => ' '.repeat(4) + pair.join(', '))
    .join(',\n');

console.log(`const uint32_t excluded_subnets[] = {\n${array}\n};`);