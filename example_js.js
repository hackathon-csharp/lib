const datapack = require('./datapacklib.js');
const assert = require('assert');
const { TextEncoder } = require('util');

const message = 'Hello, IR! Привет мир!';
const messageBytes = new TextEncoder().encode(message);

const receivedPackages = [];
datapack.setOnPacketReceived((pkg) => {
  receivedPackages.push(pkg);
});

datapack.setSendData(message);
const commands = datapack.getSendCommands();

assert(commands.length > 0, 'Commands should be generated');

for (const command of commands) {
  datapack.feed(command);
}

const received = datapack.getReceivedData();

let matches = true;
for (let i = 0; i < messageBytes.length; i += 1) {
  if (received[i] !== messageBytes[i]) {
    matches = false;
    break;
  }
}

assert(matches, 'Decoded bytes should match original message bytes');
assert(receivedPackages.length > 0, 'Should trigger packet callback');

console.log('JS datapack example passed. Commands:', commands.length);
