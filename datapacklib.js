(function (globalScope, factory) {
  if (typeof module === "object" && typeof module.exports === "object") {
    module.exports = factory();
  } else if (typeof define === "function" && define.amd) {
    define([], factory);
  } else {
    const namespace = factory();
    globalScope.datapack = namespace;
  }
})(typeof globalThis !== "undefined" ? globalThis : typeof self !== "undefined" ? self : this, function () {
  "use strict";

  const DATAPACKLIB_MAX_PAYLOAD = 512;
  const DATAPACKLIB_MAX_SIGNAL_CHANGES = ((DATAPACKLIB_MAX_PAYLOAD + 9) * 8) + 32;

  const LightLevel = Object.freeze({
    Off: 0,
    White: 1,
    Red: 2,
    Green: 3,
    Blue: 4
  });

  const SYMBOL_TO_COLOR = [
    LightLevel.Red,
    LightLevel.Green,
    LightLevel.Blue,
    LightLevel.White
  ];

  const COLOR_TO_SYMBOL = new Map([
    [LightLevel.Red, 0],
    [LightLevel.Green, 1],
    [LightLevel.Blue, 2],
    [LightLevel.White, 3]
  ]);

  const LIGHT_LEVEL_VALUES = new Set(Object.values(LightLevel));

  function cloneValue(value) {
    if (value == null) {
      return value;
    }
    if (ArrayBuffer.isView(value)) {
      return value.slice();
    }
    if (Array.isArray(value)) {
      return value.slice();
    }
    if (typeof value === "object") {
      return { ...value };
    }
    return value;
  }

  function toPositiveInteger(value, fallback) {
    if (Number.isInteger(value) && value > 0) {
      return value;
    }
    return fallback;
  }

  function toFiniteNumber(value, fallback) {
    return Number.isFinite(value) ? value : fallback;
  }

  function toLightLevel(value, fallback) {
    return LIGHT_LEVEL_VALUES.has(value) ? value : fallback;
  }

  function toUInt16(value, fallback) {
    if (Number.isInteger(value) && value >= 0 && value <= 0xFFFF) {
      return value & 0xFFFF;
    }
    return fallback;
  }

  function toUInt8(value, fallback) {
    if (Number.isInteger(value) && value >= 0 && value <= 0xFF) {
      return value & 0xFF;
    }
    return fallback;
  }

  function toPayloadLimit(value, fallback) {
    const candidate = toPositiveInteger(value, fallback);
    return Math.min(candidate, DATAPACKLIB_MAX_PAYLOAD);
  }

  class StaticVector {
    constructor(capacity) {
      if (!Number.isInteger(capacity) || capacity <= 0) {
        throw new RangeError("capacity must be a positive integer");
      }
      this._capacity = capacity;
      this._data = new Array(capacity);
      this._size = 0;
    }

    clear() {
      this._size = 0;
    }

    get size() {
      return this._size;
    }

    get capacity() {
      return this._capacity;
    }

    push(value) {
      if (this._size >= this._capacity) {
        return false;
      }
      this._data[this._size++] = cloneValue(value);
      return true;
    }

    append(data, count) {
      if (data == null) {
        return count === undefined || count === 0;
      }
      if (count !== undefined && (!Number.isInteger(count) || count < 0)) {
        return false;
      }

      let items;
      if (ArrayBuffer.isView(data)) {
        const limit = count === undefined ? data.length : Math.min(count, data.length);
        items = Array.from(data.subarray(0, limit));
      } else {
        const array = Array.isArray(data) ? data : Array.from(data);
        const limit = count === undefined ? array.length : Math.min(count, array.length);
        items = array.slice(0, limit);
      }

      if (this._size + items.length > this._capacity) {
        return false;
      }

      for (let i = 0; i < items.length; ++i) {
        this._data[this._size + i] = cloneValue(items[i]);
      }
      this._size += items.length;
      return true;
    }

    get(index) {
      if (!Number.isInteger(index) || index < 0 || index >= this._size) {
        throw new RangeError("index out of bounds");
      }
      return this._data[index];
    }

    dataSlice() {
      return this._data.slice(0, this._size);
    }

    toUint8Array() {
      const view = new Uint8Array(this._size);
      for (let i = 0; i < this._size; ++i) {
        view[i] = this._data[i] & 0xFF;
      }
      return view;
    }

    [Symbol.iterator]() {
      return this.dataSlice()[Symbol.iterator]();
    }
  }

  const StaticBuffer = StaticVector;

  class SignalBuffer extends StaticVector {
    constructor(capacity = DATAPACKLIB_MAX_SIGNAL_CHANGES) {
      super(capacity);
    }

    push(change) {
      if (typeof change !== "object" || change === null) {
        throw new TypeError("SignalChange must be an object");
      }
      const level = change.level;
      const duration = change.duration;
      if (!LIGHT_LEVEL_VALUES.has(level)) {
        throw new TypeError("Invalid light level");
      }
      if (!Number.isFinite(duration) || duration <= 0) {
        throw new RangeError("Duration must be a positive number");
      }
      const normalizedDuration = Math.round(duration);
      if (normalizedDuration <= 0) {
        throw new RangeError("Duration must round to a positive integer");
      }
      return super.push({ level, duration: normalizedDuration });
    }

    data() {
      return this.dataSlice();
    }
  }

  class ProtocolConfig {
    constructor(options = {}) {
      if (options instanceof ProtocolConfig) {
        options = { ...options };
      }
      this.unitDurationMicros = toPositiveInteger(options.unitDurationMicros, 600);
      this.preambleMarkUnits = toPositiveInteger(options.preambleMarkUnits, 16);
      this.preambleSpaceUnits = toPositiveInteger(options.preambleSpaceUnits, 8);
      this.symbolMarkUnits = toPositiveInteger(options.symbolMarkUnits, 1);
      this.separatorUnits = toPositiveInteger(options.separatorUnits, 1);
      this.frameGapUnits = toPositiveInteger(options.frameGapUnits, 12);
      this.preambleColor = toLightLevel(options.preambleColor, LightLevel.White);
      const drift = toFiniteNumber(options.allowedDriftFraction, 0.20);
      this.allowedDriftFraction = drift > 0 ? drift : 0.20;
      this.maxPayloadBytes = toPayloadLimit(options.maxPayloadBytes, DATAPACKLIB_MAX_PAYLOAD);
      this.magic = toUInt16(options.magic, 0xC39A);
      this.ender = toUInt16(options.ender, 0x51AA);
      this.version = toUInt8(options.version, 1);
    }

    tolerance(expectedUnits) {
      let fraction = Number.isFinite(this.allowedDriftFraction) ? this.allowedDriftFraction : 0.20;
      if (fraction < 0.01) {
        fraction = 0.01;
      }
      let tol = Math.round(expectedUnits * fraction);
      if (tol < 1) {
        tol = 1;
      }
      return tol;
    }

    clone() {
      return new ProtocolConfig(this);
    }
  }

  function symbolToColor(symbol) {
    return SYMBOL_TO_COLOR[symbol & 0x03];
  }

  function colorToSymbol(level) {
    return COLOR_TO_SYMBOL.has(level) ? COLOR_TO_SYMBOL.get(level) : null;
  }

  function computeCrc16(data, length = data.length) {
    if (!ArrayBuffer.isView(data) && !Array.isArray(data)) {
      throw new TypeError("data must be an array or typed array");
    }
    const poly = 0x1021;
    let crc = 0xFFFF;
    for (let i = 0; i < length; ++i) {
      crc ^= (data[i] & 0xFF) << 8;
      for (let bit = 0; bit < 8; ++bit) {
        if (crc & 0x8000) {
          crc = ((crc << 1) ^ poly) & 0xFFFF;
        } else {
          crc = (crc << 1) & 0xFFFF;
        }
      }
    }
    return crc & 0xFFFF;
  }

  class Encoder {
    constructor(config = {}) {
      this._config = new ProtocolConfig(config);
      this._valid = this._validateConfig(this._config);
    }

    get config() {
      return this._config.clone();
    }

    configure(config) {
      this._config = new ProtocolConfig(config);
      this._valid = this._validateConfig(this._config);
      return this._valid;
    }

    isValid() {
      return this._valid;
    }

    encode(payload, length, outBuffer) {
      if (!this._valid) {
        return false;
      }

      let payloadView;
      if (payload == null) {
        if (length && length > 0) {
          return false;
        }
        payloadView = new Uint8Array(0);
        length = 0;
      } else if (ArrayBuffer.isView(payload)) {
        payloadView = new Uint8Array(payload.buffer, payload.byteOffset, payload.byteLength);
      } else if (Array.isArray(payload)) {
        payloadView = Uint8Array.from(payload);
      } else {
        throw new TypeError("payload must be a Uint8Array, TypedArray, or Array");
      }

      if (typeof length === "number") {
        if (!Number.isInteger(length) || length < 0 || length > payloadView.length) {
          return false;
        }
        payloadView = payloadView.subarray(0, length);
      } else {
        length = payloadView.length;
      }

      if (length > this._config.maxPayloadBytes || length > DATAPACKLIB_MAX_PAYLOAD) {
        return false;
      }
      const frame = new StaticVector(DATAPACKLIB_MAX_PAYLOAD + 9);
      const output = outBuffer instanceof SignalBuffer ? outBuffer : new SignalBuffer();
      output.clear();

      const pushByte = (byte) => frame.push(byte & 0xFF);
      if (!pushByte(this._config.magic >> 8) ||
          !pushByte(this._config.magic & 0xFF) ||
          !pushByte(this._config.version)) {
        return false;
      }

      const lengthField = length & 0xFFFF;
      if (!pushByte(lengthField >> 8) || !pushByte(lengthField & 0xFF)) {
        return false;
      }

      const crc = computeCrc16(payloadView, length);
      if (!pushByte(crc >> 8) || !pushByte(crc & 0xFF)) {
        return false;
      }

      if (!frame.append(payloadView, length)) {
        return false;
      }

      if (!pushByte(this._config.ender >> 8) || !pushByte(this._config.ender & 0xFF)) {
        return false;
      }

      let ok = true;
      const writer = {
        unitDuration: this._config.unitDurationMicros,
        buffer: output,
        emit(level, units) {
          if (!ok || units <= 0) {
            return;
          }
          const change = {
            level,
            duration: Math.round(units * this.unitDuration)
          };
          if (!this.buffer.push(change)) {
            ok = false;
          }
        }
      };

      writer.emit(this._config.preambleColor, this._config.preambleMarkUnits);
      writer.emit(LightLevel.Off, this._config.preambleSpaceUnits);

      const frameData = frame.dataSlice();
      for (let i = 0; i < frameData.length; ++i) {
        const byte = frameData[i];
        for (let shift = 6; shift >= 0; shift -= 2) {
          const symbol = (byte >> shift) & 0x03;
          writer.emit(symbolToColor(symbol), this._config.symbolMarkUnits);
          writer.emit(LightLevel.Off, this._config.separatorUnits);
        }
      }

      writer.emit(LightLevel.Off, this._config.frameGapUnits);

      if (!ok) {
        output.clear();
        return false;
      }

      return true;
    }

    _validateConfig(config) {
      if (config.unitDurationMicros <= 0) {
        return false;
      }
      if (config.symbolMarkUnits <= 0 || config.separatorUnits <= 0) {
        return false;
      }
      if (config.preambleMarkUnits <= 0 || config.preambleSpaceUnits <= 0) {
        return false;
      }
      if (config.maxPayloadBytes <= 0 || config.maxPayloadBytes > DATAPACKLIB_MAX_PAYLOAD) {
        return false;
      }
      return true;
    }
  }

  class DecoderStats {
    constructor() {
      this.framesDecoded = 0;
      this.magicMismatches = 0;
      this.headerRejects = 0;
      this.lengthViolations = 0;
      this.crcFailures = 0;
      this.enderMismatches = 0;
      this.durationRejections = 0;
      this.markRejections = 0;
      this.truncatedFrames = 0;
    }

    clone() {
      return { ...this };
    }
  }

  class Decoder {
    constructor(callback = null, context = null, config = {}) {
      if (callback && typeof callback === "object" && !(callback instanceof Function)) {
        config = callback;
        callback = null;
        context = null;
      }
      this._config = new ProtocolConfig(config);
      this._valid = this._validateConfig(this._config);
      this._callback = typeof callback === "function" ? callback : null;
      this._callbackContext = context ?? null;
      this._stats = new DecoderStats();
      this._frameBuffer = new StaticVector(DATAPACKLIB_MAX_PAYLOAD + 9);
      this._state = "Idle";
      this._currentByte = 0;
      this._bitsFilled = 0;
      this._expectedPayloadLength = 0;
      this._payloadLengthKnown = false;
      this._pendingSymbol = 0;
      this._frameActive = false;
    }

    get config() {
      return this._config.clone();
    }

    get stats() {
      return this._stats.clone();
    }

    isValid() {
      return this._valid;
    }

    configure(config) {
      this._config = new ProtocolConfig(config);
      this._valid = this._validateConfig(this._config);
      this.reset();
      return this._valid;
    }

    setCallback(callback, context = null) {
      this._callback = typeof callback === "function" ? callback : null;
      this._callbackContext = context ?? null;
    }

    reset() {
      this._state = "Idle";
      this._frameBuffer.clear();
      this._currentByte = 0;
      this._bitsFilled = 0;
      this._expectedPayloadLength = 0;
      this._payloadLengthKnown = false;
      this._pendingSymbol = 0;
      this._frameActive = false;
    }

    feed(change) {
      if (!this._valid || !change) {
        return;
      }

      const level = change.level;
      const duration = change.duration;
      if (!LIGHT_LEVEL_VALUES.has(level)) {
        this._stats.markRejections += 1;
        this._abortFrame();
        return;
      }
      if (!Number.isFinite(duration) || duration <= 0) {
        return;
      }

      const ratio = duration / this._config.unitDurationMicros;
      let units = Math.round(ratio);
      const error = Math.abs(ratio - units);
      const driftLimit = Math.max(this._config.allowedDriftFraction, 0.01);
      const invalidTiming = units <= 0 || error > driftLimit;
      const preambleUnits = this._config.preambleMarkUnits;

      if (invalidTiming) {
        this._stats.durationRejections += 1;
        this._abortFrame();
        if (level === this._config.preambleColor && this._matches(units, preambleUnits)) {
          this._state = "WaitSpace";
        }
        return;
      }

      switch (this._state) {
        case "Idle":
          if (level === this._config.preambleColor && this._matches(units, preambleUnits)) {
            this._state = "WaitSpace";
          }
          break;
        case "WaitSpace":
          if (level === LightLevel.Off && this._matches(units, this._config.preambleSpaceUnits)) {
            this._startFrame();
          } else if (level === this._config.preambleColor && this._matches(units, preambleUnits)) {
            this._state = "WaitSpace";
          } else {
            this._abortFrame();
            if (level === this._config.preambleColor && this._matches(units, preambleUnits)) {
              this._state = "WaitSpace";
            }
          }
          break;
        case "ReadMark":
          if (level === LightLevel.Off) {
            this._stats.markRejections += 1;
            this._abortFrame();
            if (level === this._config.preambleColor && this._matches(units, preambleUnits)) {
              this._state = "WaitSpace";
            }
            break;
          }
          {
            const symbol = this._decodeSymbol(units, level);
            if (symbol === null) {
              this._stats.markRejections += 1;
              this._abortFrame();
              if (level === this._config.preambleColor && this._matches(units, preambleUnits)) {
                this._state = "WaitSpace";
              }
              break;
            }
            this._pendingSymbol = symbol;
            this._state = "ReadSpace";
          }
          break;
        case "ReadSpace":
          if (level !== LightLevel.Off) {
            this._stats.durationRejections += 1;
            this._abortFrame();
            if (level === this._config.preambleColor && this._matches(units, preambleUnits)) {
              this._state = "WaitSpace";
            }
            break;
          }
          if (!this._matches(units, this._config.separatorUnits) && units < this._config.separatorUnits) {
            this._stats.durationRejections += 1;
            this._abortFrame();
            if (level === this._config.preambleColor && this._matches(units, preambleUnits)) {
              this._state = "WaitSpace";
            }
            break;
          }
          this._handleSymbol(this._pendingSymbol);
          if (this._state === "ReadSpace") {
            this._state = "ReadMark";
          }
          break;
        default:
          this._state = "Idle";
          break;
      }
    }

    _startFrame() {
      this._frameBuffer.clear();
      this._currentByte = 0;
      this._bitsFilled = 0;
      this._expectedPayloadLength = 0;
      this._payloadLengthKnown = false;
      this._pendingSymbol = 0;
      this._frameActive = true;
      this._state = "ReadMark";
    }

    _handleSymbol(symbol) {
      this._currentByte = ((this._currentByte << 2) | (symbol & 0x03)) & 0xFF;
      this._bitsFilled += 2;
      if (this._bitsFilled === 8) {
        if (!this._frameBuffer.push(this._currentByte)) {
          this._abortFrame();
          return;
        }
        this._currentByte = 0;
        this._bitsFilled = 0;

        const frameSize = this._frameBuffer.size;
        if (frameSize === 5) {
          this._expectedPayloadLength = ((this._frameBuffer.get(3) & 0xFF) << 8) | (this._frameBuffer.get(4) & 0xFF);
          this._payloadLengthKnown = true;
          if (this._expectedPayloadLength > this._config.maxPayloadBytes) {
            this._stats.lengthViolations += 1;
            this._abortFrame();
            return;
          }
        }

        if (this._payloadLengthKnown) {
          const totalBytesNeeded = 9 + this._expectedPayloadLength;
          if (frameSize > totalBytesNeeded) {
            this._abortFrame();
            return;
          }
          if (frameSize === totalBytesNeeded) {
            this._finalizeFrame();
          }
        }
      }
    }

    _finalizeFrame() {
      const frameSize = this._frameBuffer.size;
      if (frameSize < 9) {
        this._stats.headerRejects += 1;
        this._abortFrame();
        return;
      }

      const magic = ((this._frameBuffer.get(0) & 0xFF) << 8) | (this._frameBuffer.get(1) & 0xFF);
      if (magic !== this._config.magic) {
        this._stats.magicMismatches += 1;
        this._abortFrame();
        return;
      }

      if ((this._frameBuffer.get(2) & 0xFF) !== (this._config.version & 0xFF)) {
        this._stats.headerRejects += 1;
        this._abortFrame();
        return;
      }

      const payloadLength = ((this._frameBuffer.get(3) & 0xFF) << 8) | (this._frameBuffer.get(4) & 0xFF);
      if (payloadLength > this._config.maxPayloadBytes) {
        this._stats.lengthViolations += 1;
        this._abortFrame();
        return;
      }

      const expectedCrc = ((this._frameBuffer.get(5) & 0xFF) << 8) | (this._frameBuffer.get(6) & 0xFF);

      if (frameSize !== 9 + payloadLength) {
        this._stats.truncatedFrames += 1;
        this._abortFrame();
        return;
      }

      const ender = ((this._frameBuffer.get(frameSize - 2) & 0xFF) << 8) |
        (this._frameBuffer.get(frameSize - 1) & 0xFF);
      if (ender !== this._config.ender) {
        this._stats.enderMismatches += 1;
        this._abortFrame();
        return;
      }

      const payloadStart = 7;
      const payloadView = this._frameBuffer.toUint8Array().subarray(payloadStart, payloadStart + payloadLength);
      const computedCrc = computeCrc16(payloadView, payloadLength);
      if (computedCrc !== expectedCrc) {
        this._stats.crcFailures += 1;
        this._abortFrame();
        return;
      }

      if (this._callback) {
        const payloadCopy = payloadLength ? payloadView.slice() : new Uint8Array(0);
        this._callback(payloadCopy, payloadLength, this._callbackContext);
      }

      this._stats.framesDecoded += 1;
      this.reset();
    }

    _abortFrame() {
      if (this._frameActive) {
        this._stats.truncatedFrames += 1;
      }
      this.reset();
    }

    _matches(units, expected) {
      const diff = Math.abs(units - expected);
      return diff <= this._config.tolerance(expected);
    }

    _decodeSymbol(units, level) {
      if (!this._matches(units, this._config.symbolMarkUnits)) {
        return null;
      }
      return colorToSymbol(level);
    }

    _validateConfig(config) {
      if (config.unitDurationMicros <= 0) {
        return false;
      }
      if (config.symbolMarkUnits <= 0 || config.separatorUnits <= 0) {
        return false;
      }
      if (config.preambleMarkUnits <= 0 || config.preambleSpaceUnits <= 0) {
        return false;
      }
      if (config.maxPayloadBytes <= 0 || config.maxPayloadBytes > DATAPACKLIB_MAX_PAYLOAD) {
        return false;
      }
      return true;
    }
  }

  return Object.freeze({
    DATAPACKLIB_MAX_PAYLOAD,
    DATAPACKLIB_MAX_SIGNAL_CHANGES,
    LightLevel,
    ProtocolConfig,
    SignalBuffer,
    Encoder,
    Decoder,
    DecoderStats,
    computeCrc16,
    StaticVector,
    StaticBuffer: StaticVector
  });
});
