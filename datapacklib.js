/*
 * datapacklib.js
 * JavaScript port of the datapack encoder/decoder for use in browsers and Node.js.
 */

(function (globalScope, factory) {
	const api = factory();

	if (typeof module === "object" && typeof module.exports === "object") {
		module.exports = api;
	} else if (typeof define === "function" && define.amd) {
		define([], () => api);
	}

	if (globalScope && typeof globalScope === "object") {
		globalScope.datapack = api;
	}
})(typeof globalThis !== "undefined" ? globalThis : typeof self !== "undefined" ? self : this, function () {
	"use strict";

	const SEND_BUFFER_CAPACITY = 256;
	const COMMAND_BUFFER_CAPACITY = 4096;

	const LightLevel = Object.freeze({
		Off: 0,
		White: 1,
		Red: 2,
		Green: 3,
		Blue: 4,
	});

	const LEVEL_SEQUENCE = [
		LightLevel.Off,
		LightLevel.White,
		LightLevel.Red,
		LightLevel.Green,
		LightLevel.Blue,
	];

	let minDuration = 60;
	let signalDuration = 125;

	const sendBuffer = [];
	const sendCommands = [];
	const receiveBuffer = new Uint16Array(SEND_BUFFER_CAPACITY);

	let window = 0x00bc614e;
	let prevValue = LightLevel.Off;
	let packetHandler = null;

	const clampToByte = (value) => value & 0xff;

	const isValidLightLevel = (value) => LEVEL_SEQUENCE.includes(value);

	const clearSendBuffer = () => {
		sendBuffer.length = 0;
	};

	const pushWordToSendBuffer = (word) => {
		if (sendBuffer.length >= SEND_BUFFER_CAPACITY) {
			throw new RangeError(
				`Send buffer capacity of ${SEND_BUFFER_CAPACITY} words exceeded`
			);
		}
		sendBuffer.push(word & 0xffff);
	};

	const clearSendCommands = () => {
		sendCommands.length = 0;
	};

	const pushSendCommand = (command) => {
		if (sendCommands.length >= COMMAND_BUFFER_CAPACITY) {
			throw new RangeError(
				`Send command capacity of ${COMMAND_BUFFER_CAPACITY} entries exceeded`
			);
		}
		sendCommands.push({ value: command.value, duration: command.duration });
	};

	const getDbit = (prev, curr) => {
		switch (prev) {
			case LightLevel.Off:
				switch (curr) {
					case LightLevel.Off:
					case LightLevel.White:
						return 0;
					case LightLevel.Red:
						return 1;
					case LightLevel.Green:
						return 2;
					case LightLevel.Blue:
						return 3;
					default:
						return 0;
				}
			case LightLevel.White:
				switch (curr) {
					case LightLevel.White:
					case LightLevel.Off:
						return 0;
					case LightLevel.Red:
						return 1;
					case LightLevel.Green:
						return 2;
					case LightLevel.Blue:
						return 3;
					default:
						return 0;
				}
			case LightLevel.Red:
				switch (curr) {
					case LightLevel.Red:
					case LightLevel.Off:
						return 0;
					case LightLevel.White:
						return 1;
					case LightLevel.Green:
						return 2;
					case LightLevel.Blue:
						return 3;
					default:
						return 0;
				}
			case LightLevel.Green:
				switch (curr) {
					case LightLevel.Green:
					case LightLevel.Off:
						return 0;
					case LightLevel.White:
						return 1;
					case LightLevel.Red:
						return 2;
					case LightLevel.Blue:
						return 3;
					default:
						return 0;
				}
			case LightLevel.Blue:
				switch (curr) {
					case LightLevel.Blue:
					case LightLevel.Off:
						return 0;
					case LightLevel.White:
						return 1;
					case LightLevel.Red:
						return 2;
					case LightLevel.Green:
						return 3;
					default:
						return 0;
				}
			default:
				return 0;
		}
	};

	const getLightForDbit = (prev, data) => {
		const dbit = data & 0x03;
		for (const level of LEVEL_SEQUENCE) {
			if (level === prev) {
				continue;
			}
			if (getDbit(prev, level) === dbit) {
				return level;
			}
		}
		return LightLevel.Off;
	};

	const crc8 = (word, index) => {
		const bytes = [word & 0xff, (word >> 8) & 0xff, index & 0xff];
		let crc = 0;
		for (let i = 0; i < bytes.length; i += 1) {
			crc ^= bytes[i];
			for (let bit = 0; bit < 8; bit += 1) {
				if (crc & 0x80) {
					crc = ((crc << 1) ^ 0x07) & 0xff;
				} else {
					crc = (crc << 1) & 0xff;
				}
			}
		}
		return crc;
	};

	const encode = () => {
		let previous = LightLevel.Off;
		clearSendCommands();

		for (let i = 0; i < sendBuffer.length; i += 1) {
			const word = sendBuffer[i];
			const index = i & 0xff;
			const crc = crc8(word, index);

			const segments = [
				index,
				clampToByte(word),
				clampToByte(word >> 8),
				crc,
			];

			for (let seg = 0; seg < segments.length; seg += 1) {
				const value = segments[seg];
				for (let bitPair = 3; bitPair >= 0; bitPair -= 1) {
					const twobits = (value >> (bitPair * 2)) & 0x03;
					const next = getLightForDbit(previous, twobits);
					pushSendCommand({ value: next, duration: signalDuration });
					previous = next;
				}
			}
		}
	};

	const toUint8Array = (input) => {
		if (input instanceof Uint8Array) {
			return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
		}
		if (input instanceof ArrayBuffer) {
			return new Uint8Array(input);
		}
		if (ArrayBuffer.isView(input)) {
			return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
		}
		if (typeof input === "string") {
			return typeof TextEncoder !== "undefined"
				? new TextEncoder().encode(input)
				: Uint8Array.from(input.split("").map((ch) => ch.charCodeAt(0)));
		}
		if (Array.isArray(input)) {
			return Uint8Array.from(input);
		}
		throw new TypeError(
			"setSendData expects a string, ArrayBuffer, Uint8Array or array-like input"
		);
	};

	const setSendData = (data, byteLength) => {
		const bytes = toUint8Array(data);
		const limit = Math.min(
			typeof byteLength === "number" ? byteLength : bytes.length,
			SEND_BUFFER_CAPACITY * 2
		);

		clearSendBuffer();

		for (let i = 0; i < limit; i += 2) {
			let word = bytes[i];
			if (i + 1 < limit) {
				word |= bytes[i + 1] << 8;
			}
			pushWordToSendBuffer(word);
		}

		encode();
	};

	const getSendBuffer = () => sendBuffer.slice();

	const getSendCommands = () => sendCommands.slice();

	const getReceivedWords = () => receiveBuffer.slice();

	const getReceivedData = () => {
		const output = new Uint8Array(receiveBuffer.length * 2);
		let offset = 0;
		for (let i = 0; i < receiveBuffer.length; i += 1) {
			const word = receiveBuffer[i];
			output[offset] = word & 0xff;
			output[offset + 1] = (word >> 8) & 0xff;
			offset += 2;
		}
		return output;
	};

	const unpackAndCheck = (packet) => {
		const value = packet >>> 0;
		const index = (value >>> 24) & 0xff;
		const wordLow = (value >>> 16) & 0xff;
		const wordHigh = (value >>> 8) & 0xff;
		const crc = value & 0xff;
		const word = ((wordHigh << 8) | wordLow) & 0xffff;

		const expected = crc8(word, index);

		if (crc === expected) {
			return { valid: true, index, word };
		}
		return { valid: false, index: 0, word: 0 };
	};

	const feed = (signalChange) => {
		if (!signalChange || typeof signalChange !== "object") {
			throw new TypeError("feed expects an object with value and duration fields");
		}

		const { value, duration } = signalChange;

		if (!isValidLightLevel(value)) {
			throw new RangeError("signalChange.value must be a valid LightLevel");
		}

		if (typeof duration !== "number" || Number.isNaN(duration)) {
			throw new TypeError("signalChange.duration must be a finite number");
		}

		if (duration < minDuration) {
			prevValue = value;
			return null;
		}

		const dataBits = getDbit(prevValue, value);
		window = (((window << 2) | dataBits) >>> 0) & 0xffffffff;

		const unpacked = unpackAndCheck(window);
		if (unpacked.valid && unpacked.index < receiveBuffer.length) {
			receiveBuffer[unpacked.index] = unpacked.word;
			if (typeof packetHandler === "function") {
				packetHandler({ ...unpacked });
			}
		}

		prevValue = value;
		return unpacked.valid ? { ...unpacked } : null;
	};

	const setOnPacketReceived = (handler) => {
		if (handler !== null && typeof handler !== "function") {
			throw new TypeError("Packet handler must be a function or null");
		}
		packetHandler = handler;
	};

	const resetDecoder = () => {
		window = 0x00bc614e;
		prevValue = LightLevel.Off;
		receiveBuffer.fill(0);
	};

	const resetEncoder = () => {
		clearSendBuffer();
		clearSendCommands();
	};

	const setMinDuration = (value) => {
		if (typeof value !== "number" || !Number.isFinite(value) || value < 0) {
			throw new RangeError("minDuration must be a non-negative finite number");
		}
		minDuration = value;
	};

	const setSignalDuration = (value) => {
		if (typeof value !== "number" || !Number.isFinite(value) || value <= 0) {
			throw new RangeError("signal duration must be a positive finite number");
		}
		signalDuration = value;
	};

	const getMinDuration = () => minDuration;
	const getSignalDuration = () => signalDuration;

	return {
		LightLevel,
		getMinDuration,
		setMinDuration,
		getSignalDuration,
		setSignalDuration,
		setOnPacketReceived,
		setSendData,
		getSendBuffer,
		getSendCommands,
		getReceivedWords,
		getReceivedData,
		feed,
		resetDecoder,
		resetEncoder,
	};
});

