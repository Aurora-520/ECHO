const SYNC0 = 0xA5;
const SYNC1 = 0x5A;
const VERSION = 1;
const TYPE_CONTROL = 1;
const TYPE_PARAMETER_SET = 2;
const TYPE_PARAMETER_ACK = 3;
const CONTROL_PAYLOAD = 40;
const ACK_PAYLOAD = 16;
const MAX_PAYLOAD = 128;
const MAX_POINTS = 2000;
const PARAMETER_ACK_TIMEOUT_MS = 1000;
const PARAMETER_MAX_ATTEMPTS = 3;
const MAX_CAPTURE_POINTS = 120000;

const channelDefinitions = [
  { key: "setpoint", label: "Setpoint", color: "#42d3c5", enabled: true },
  { key: "measurement", label: "Measurement", color: "#f2b66d", enabled: true },
  { key: "output", label: "Output", color: "#8db7ff", enabled: true },
  { key: "auxiliary", label: "Kp / Aux", color: "#e18be4", enabled: true },
  { key: "execution", label: "Execution us", color: "#9bd47d", enabled: false },
  { key: "jitter", label: "Jitter us", color: "#ff927e", enabled: false }
];

const parameterDefinitions = {
  kp: { id: 1, min: 0, max: 1000 },
  ki: { id: 2, min: 0, max: 1000 },
  kd: { id: 3, min: 0, max: 1000 },
  target: { id: 4, min: -10000, max: 10000 }
};

const stat = {
  bytes: 0,
  frames: 0,
  acks: 0,
  crcErrors: 0,
  badLength: 0,
  badVersion: 0,
  syncDrops: 0,
  seqGaps: 0,
  seqResets: 0,
  invalidPayload: 0,
  overflow: 0,
  unexpectedAcks: 0,
  captureTruncated: 0
};

const points = [];
const selectedChannels = new Set();
const capturedPoints = [];
const pendingTransactions = new Map();
let port = null;
let reader = null;
let writer = null;
let readLoopPromise = null;
let outgoingSequence = 1;
let nextTransaction =
  crypto.getRandomValues(new Uint32Array(1))[0] || 1;
let firstDeviceTimestamp = null;
let latestFrame = null;

const baudRate = document.querySelector("#baudRate");
let connectionPhase = "idle";
const connectButton = document.querySelector("#connectButton");
const disconnectButton = document.querySelector("#disconnectButton");
const clearButton = document.querySelector("#clearButton");
const exportButton = document.querySelector("#exportButton");
const sendButton = document.querySelector("#sendButton");
const connectionStatus = document.querySelector("#connectionStatus");
const browserStatus = document.querySelector("#browserStatus");
const lastSample = document.querySelector("#lastSample");
const tuningStatus = document.querySelector("#tuningStatus");
const channelsElement = document.querySelector("#channels");
const statsElement = document.querySelector("#stats");
const canvas = document.querySelector("#chart");
const context = canvas.getContext("2d");

function crc16(data, start, end) {
  let crc = 0xFFFF;
  for (let index = start; index < end; index += 1) {
    crc ^= data[index] << 8;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc & 0x8000) !== 0
        ? ((crc << 1) ^ 0x1021) & 0xFFFF
        : (crc << 1) & 0xFFFF;
    }
  }
  return crc;
}

class Decoder {
  constructor(onFrame) {
    this.buffer = new Uint8Array(0);
    this.onFrame = onFrame;
    this.lastSequence = null;
  }

  reset() {
    this.buffer = new Uint8Array(0);
    this.lastSequence = null;
  }

  push(chunk) {
    stat.bytes += chunk.length;
    const joined = new Uint8Array(this.buffer.length + chunk.length);
    joined.set(this.buffer);
    joined.set(chunk, this.buffer.length);
    this.buffer = joined;
    if (this.buffer.length > 8192) {
      stat.overflow += 1;
      this.buffer = this.buffer.slice(-4096);
    }
    this.parse();
  }

  parse() {
    while (this.buffer.length >= 2) {
      let syncPosition = -1;
      for (let index = 0; index + 1 < this.buffer.length; index += 1) {
        if (this.buffer[index] === SYNC0 &&
            this.buffer[index + 1] === SYNC1) {
          syncPosition = index;
          break;
        }
      }

      if (syncPosition < 0) {
        const keep = this.buffer[this.buffer.length - 1] === SYNC0 ? 1 : 0;
        stat.syncDrops += this.buffer.length - keep;
        this.buffer = keep
          ? this.buffer.slice(-1)
          : new Uint8Array(0);
        return;
      }
      if (syncPosition > 0) {
        stat.syncDrops += syncPosition;
        this.buffer = this.buffer.slice(syncPosition);
      }
      if (this.buffer.length < 16) {
        return;
      }

      const version = this.buffer[2];
      const type = this.buffer[3];
      const payloadLength = this.buffer[4] | (this.buffer[5] << 8);
      if (version !== VERSION) {
        stat.badVersion += 1;
        this.buffer = this.buffer.slice(1);
        continue;
      }
      if (payloadLength > MAX_PAYLOAD ||
          (type === TYPE_CONTROL && payloadLength !== CONTROL_PAYLOAD) ||
          (type === TYPE_PARAMETER_ACK && payloadLength !== ACK_PAYLOAD)) {
        stat.badLength += 1;
        this.buffer = this.buffer.slice(1);
        continue;
      }

      const totalLength = 16 + payloadLength;
      if (this.buffer.length < totalLength) {
        return;
      }
      const receivedCrc = this.buffer[14 + payloadLength] |
        (this.buffer[15 + payloadLength] << 8);
      const calculatedCrc = crc16(this.buffer, 2, 14 + payloadLength);
      if (receivedCrc !== calculatedCrc) {
        stat.crcErrors += 1;
        this.buffer = this.buffer.slice(1);
        continue;
      }

      const frame = this.buffer.slice(0, totalLength);
      this.buffer = this.buffer.slice(totalLength);
      const view = new DataView(
        frame.buffer, frame.byteOffset, frame.byteLength);
      const sequence = view.getUint32(6, true);
      const timestamp = view.getUint32(10, true);
      if (this.lastSequence !== null) {
        const delta = (sequence - this.lastSequence) >>> 0;
        if (delta > 1 && delta < 0x80000000) {
          stat.seqGaps += delta - 1;
        } else if (delta >= 0x80000000) {
          stat.seqResets += 1;
        }
      }
      this.lastSequence = sequence;
      this.onFrame({ type, sequence, timestamp, frame, view });
    }
  }
}

function handleFrame(decoded) {
  if (decoded.type === TYPE_CONTROL) {
    const base = 14;
    const point = {
      sequence: decoded.sequence,
      timestamp: decoded.timestamp,
      hostMs: performance.now(),
      setpoint: decoded.view.getFloat32(base, true),
      measurement: decoded.view.getFloat32(base + 4, true),
      output: decoded.view.getFloat32(base + 8, true),
      auxiliary: decoded.view.getFloat32(base + 12, true),
      loop: decoded.view.getUint32(base + 16, true),
      period: decoded.view.getUint32(base + 20, true),
      execution: decoded.view.getUint32(base + 24, true),
      jitter: decoded.view.getUint32(base + 28, true),
      deadlineMiss: decoded.view.getUint32(base + 32, true),
      flags: decoded.view.getUint32(base + 36, true)
    };
    if (![point.setpoint, point.measurement, point.output,
          point.auxiliary].every(Number.isFinite)) {
      stat.invalidPayload += 1;
      return;
    }
    if (firstDeviceTimestamp === null) {
      firstDeviceTimestamp = point.timestamp;
    }
    points.push(point);
    if (points.length > MAX_POINTS) {
      points.shift();
    }
    capturedPoints.push(point);
    if (capturedPoints.length > MAX_CAPTURE_POINTS) {
      capturedPoints.shift();
      stat.captureTruncated += 1;
    }
    stat.frames += 1;
    latestFrame = point;
  } else if (decoded.type === TYPE_PARAMETER_ACK) {
    stat.acks += 1;
    const transactionId = decoded.view.getUint32(14, true);
    const parameterId = decoded.view.getUint16(18, true);
    const status = decoded.view.getUint8(20);
    const reserved = decoded.view.getUint8(21);
    const appliedValue = decoded.view.getFloat32(22, true);
    const applySequence = decoded.view.getUint32(26, true);
    const pending = pendingTransactions.get(transactionId);
    if (!pending) {
      stat.unexpectedAcks += 1;
      return;
    }
    const valueMismatch = status === 0 &&
      Math.abs(appliedValue - pending.value) > 0.0001;
    if (parameterId !== pending.parameterId || reserved !== 0 ||
        status > 4 || !Number.isFinite(appliedValue) || valueMismatch) {
      stat.unexpectedAcks += 1;
      tuningStatus.textContent =
        "ACK \u4e0d\u5339\u914d / transaction " + transactionId;
      return;
    }
    if (status === 4) {
      tuningStatus.textContent =
        "\u8bbe\u5907\u5fd9\uff0c\u7b49\u5f85\u91cd\u8bd5 / transaction " +
        transactionId;
      return;
    }
    clearTimeout(pending.timer);
    pendingTransactions.delete(transactionId);
    updateConnectionControls();
    tuningStatus.textContent = status === 0
      ? "\u5df2\u5e94\u7528: " + appliedValue.toFixed(5) +
        " / sequence " + applySequence
      : "\u8bbe\u5907\u62d2\u7edd: status " + status + " / id " + parameterId;
  }
}

const decoder = new Decoder(handleFrame);

function createChannelControls() {
  channelDefinitions.forEach((channel, index) => {
    const label = document.createElement("label");
    label.className = "channel swatch-" + index;
    const input = document.createElement("input");
    input.type = "checkbox";
    input.checked = channel.enabled;
    input.addEventListener("change", () => {
      if (input.checked) {
        selectedChannels.add(channel.key);
      } else {
        selectedChannels.delete(channel.key);
      }
    });
    if (channel.enabled) {
      selectedChannels.add(channel.key);
    }
    label.append(input, channel.label);
    channelsElement.append(label);
  });
}

function resizeCanvas() {
  const rect = canvas.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  const width = Math.max(1, Math.floor(rect.width * ratio));
  const height = Math.max(1, Math.floor(rect.height * ratio));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  return rect;
}

function drawChart() {
  const rect = resizeCanvas();
  const width = rect.width;
  const height = rect.height;
  context.clearRect(0, 0, width, height);

  context.strokeStyle = "#263137";
  context.lineWidth = 1;
  for (let line = 1; line < 5; line += 1) {
    const y = (height * line) / 5;
    context.beginPath();
    context.moveTo(0, y);
    context.lineTo(width, y);
    context.stroke();
  }

  const active = channelDefinitions.filter(
    channel => selectedChannels.has(channel.key));
  if (points.length < 2 || active.length === 0) {
    context.fillStyle = "#6f7f85";
    context.font = "13px Segoe UI";
    context.fillText("\u7b49\u5f85\u6709\u6548\u63a7\u5236\u5e27", 16, 26);
    requestAnimationFrame(drawChart);
    return;
  }

  let minimum = Infinity;
  let maximum = -Infinity;
  points.forEach(point => {
    active.forEach(channel => {
      const value = point[channel.key];
      minimum = Math.min(minimum, value);
      maximum = Math.max(maximum, value);
    });
  });
  if (minimum === maximum) {
    minimum -= 1;
    maximum += 1;
  } else {
    const padding = (maximum - minimum) * 0.08;
    minimum -= padding;
    maximum += padding;
  }

  active.forEach(channel => {
    context.strokeStyle = channel.color;
    context.lineWidth = 1.5;
    context.beginPath();
    points.forEach((point, index) => {
      const x = (index / (points.length - 1)) * width;
      const y = height -
        ((point[channel.key] - minimum) / (maximum - minimum)) * height;
      if (index === 0) {
        context.moveTo(x, y);
      } else {
        context.lineTo(x, y);
      }
    });
    context.stroke();
  });

  context.fillStyle = "#7f9097";
  context.font = "11px Segoe UI";
  context.fillText(maximum.toFixed(3), 7, 14);
  context.fillText(minimum.toFixed(3), 7, height - 7);
  requestAnimationFrame(drawChart);
}

function updateStatus() {
  document.querySelectorAll("[data-stat]").forEach(element => {
    element.textContent = stat[element.dataset.stat];
  });
  const rate = points.length > 1
    ? ((points.length - 1) * 1000000) /
      (((points[points.length - 1].timestamp -
         points[0].timestamp) >>> 0) || 1)
    : 0;
  statsElement.textContent =
    "Rate " + rate.toFixed(1) + " Hz | " +
    "Period " + (latestFrame ? latestFrame.period : 0) + " us | " +
    "Exec " + (latestFrame ? latestFrame.execution : 0) + " us | " +
    "Deadline " + (latestFrame ? latestFrame.deadlineMiss : 0);
  if (latestFrame) {
    lastSample.textContent =
      "seq " + latestFrame.sequence +
      " / set " + latestFrame.setpoint.toFixed(3) +
      " / measure " + latestFrame.measurement.toFixed(3) +
      " / kp " + latestFrame.auxiliary.toFixed(3);
  }
}

function setConnectionState(message, mode) {
  connectionStatus.textContent = message;
  connectionStatus.className = "status" + (mode ? " " + mode : "");
}

function updateConnectionControls() {
  connectButton.disabled = connectionPhase !== "idle";
  disconnectButton.disabled = connectionPhase === "idle";
  baudRate.disabled = connectionPhase !== "idle";
  sendButton.disabled =
    connectionPhase !== "connected" || pendingTransactions.size > 0;
}

function cancelPendingTransactions(message) {
  pendingTransactions.forEach(pending => {
    clearTimeout(pending.timer);
  });
  pendingTransactions.clear();
  if (message) {
    tuningStatus.textContent = message;
  }
  updateConnectionControls();
}

async function readLoop(activePort) {
  const activeReader = activePort.readable.getReader();
  reader = activeReader;
  try {
    while (true) {
      const result = await activeReader.read();
      if (result.done) {
        break;
      }
      if (result.value) {
        decoder.push(result.value);
      }
    }
  } finally {
    if (reader === activeReader) {
      reader = null;
    }
    activeReader.releaseLock();
  }
}

async function closeSerialResources() {
  const activeReader = reader;
  if (activeReader) {
    try {
      await activeReader.cancel();
    } catch (error) {
      console.warn(error);
    }
  }

  const activeReadLoop = readLoopPromise;
  if (activeReadLoop) {
    try {
      await activeReadLoop;
    } catch (error) {
      console.warn(error);
    }
  }
  readLoopPromise = null;
  reader = null;

  const activeWriter = writer;
  writer = null;
  if (activeWriter) {
    try {
      activeWriter.releaseLock();
    } catch (error) {
      console.warn(error);
    }
  }

  const activePort = port;
  port = null;
  if (activePort) {
    try {
      await activePort.close();
    } catch (error) {
      console.warn(error);
    }
  }
}

async function disconnect(disconnectedPort = null) {
  if (disconnectedPort && port && disconnectedPort !== port) {
    return;
  }
  if (connectionPhase === "idle" || connectionPhase === "disconnecting") {
    return;
  }

  connectionPhase = "disconnecting";
  updateConnectionControls();
  cancelPendingTransactions(
    "\u8c03\u53c2\u5df2\u53d6\u6d88\uff1a\u4e32\u53e3\u5df2\u65ad\u5f00\u3002");
  await closeSerialResources();
  decoder.reset();
  connectionPhase = "idle";
  setConnectionState("\u5df2\u65ad\u5f00");
  updateConnectionControls();
}


async function connect() {
  if (connectionPhase !== "idle") {
    return;
  }
  if (!("serial" in navigator)) {
    setConnectionState("\u6d4f\u89c8\u5668\u4e0d\u652f\u6301", "error");
    return;
  }

  connectionPhase = "connecting";
  updateConnectionControls();
  let candidatePort = null;
  let candidateWriter = null;
  try {
    candidatePort = await navigator.serial.requestPort();
    await candidatePort.open({
      baudRate: Number(baudRate.value),
      dataBits: 8,
      stopBits: 1,
      parity: "none",
      flowControl: "none"
    });
    candidateWriter = candidatePort.writable.getWriter();
    port = candidatePort;
    writer = candidateWriter;
    candidatePort = null;
    candidateWriter = null;
    connectionPhase = "connected";
    clearData();
    setConnectionState("\u5df2\u8fde\u63a5", "connected");
    const activePort = port;
    readLoopPromise = readLoop(activePort).catch(error => {
      if (port === activePort) {
        setConnectionState(error.message, "error");
        setTimeout(() => { disconnect(activePort); }, 0);
      }
    });
  } catch (error) {
    if (candidateWriter) {
      try {
        candidateWriter.releaseLock();
      } catch (releaseError) {
        console.warn(releaseError);
      }
    }
    if (candidatePort) {
      try {
        await candidatePort.close();
      } catch (closeError) {
        console.warn(closeError);
      }
    }
    connectionPhase = "idle";
    setConnectionState(error.message, "error");
  }
  updateConnectionControls();
}

function armParameterRetry(transactionId) {
  const pending = pendingTransactions.get(transactionId);
  if (!pending) {
    return;
  }
  clearTimeout(pending.timer);
  pending.timer = setTimeout(async () => {
    const current = pendingTransactions.get(transactionId);
    if (!current) {
      return;
    }
    if (current.attempts >= PARAMETER_MAX_ATTEMPTS) {
      pendingTransactions.delete(transactionId);
      tuningStatus.textContent =
        "ACK \u8d85\u65f6 / transaction " + transactionId +
        " / attempts " + current.attempts;
      updateConnectionControls();
      return;
    }
    if (connectionPhase !== "connected" || !writer) {
      pendingTransactions.delete(transactionId);
      tuningStatus.textContent =
        "\u91cd\u8bd5\u53d6\u6d88\uff1a\u4e32\u53e3\u672a\u8fde\u63a5\u3002";
      updateConnectionControls();
      return;
    }

    current.attempts += 1;
    tuningStatus.textContent =
      "\u91cd\u8bd5 " + current.attempts + "/" +
      PARAMETER_MAX_ATTEMPTS + " / transaction " + transactionId;
    try {
      await writer.write(current.frame);
      armParameterRetry(transactionId);
    } catch (error) {
      pendingTransactions.delete(transactionId);
      tuningStatus.textContent = error.message;
      updateConnectionControls();
    }
  }, PARAMETER_ACK_TIMEOUT_MS);
}


function buildParameterFrame(parameterId, value, transactionId) {
  const frame = new Uint8Array(28);
  const view = new DataView(frame.buffer);
  frame[0] = SYNC0;
  frame[1] = SYNC1;
  frame[2] = VERSION;
  frame[3] = TYPE_PARAMETER_SET;
  view.setUint16(4, 12, true);
  view.setUint32(6, outgoingSequence, true);
  outgoingSequence = (outgoingSequence + 1) >>> 0;
  view.setUint32(10, 0, true);
  view.setUint32(14, transactionId, true);
  view.setUint16(18, parameterId, true);
  frame[20] = 1;
  frame[21] = 0;
  view.setFloat32(22, value, true);
  view.setUint16(26, crc16(frame, 2, 26), true);
  return frame;
}

async function sendParameter() {
  if (!writer) {
    tuningStatus.textContent = "\u8bf7\u5148\u8fde\u63a5\u4e32\u53e3\u3002";
    return;
  }
  const name = document.querySelector("#parameterName").value;
  const value = Number(document.querySelector("#parameterValue").value);
  const definition = parameterDefinitions[name];
  if (!Number.isFinite(value) ||
      value < definition.min || value > definition.max) {
    tuningStatus.textContent =
      "\u8303\u56f4\u9519\u8bef: " + definition.min + " .. " + definition.max;
    return;
  }
  const transactionId = nextTransaction >>> 0;
  nextTransaction = (nextTransaction + 1) >>> 0;
  const requestedValue = Math.fround(value);
  const frame = buildParameterFrame(
    definition.id, requestedValue, transactionId);
  tuningStatus.textContent = "\u7b49\u5f85 ACK / transaction " + transactionId;
  pendingTransactions.set(transactionId, {
    name,
    parameterId: definition.id,
    value: requestedValue,
    frame,
    attempts: 1,
    timer: null
  });
  updateConnectionControls();
  try {
    await writer.write(frame);
    armParameterRetry(transactionId);
  } catch (error) {
    const pending = pendingTransactions.get(transactionId);
    if (pending) {
      clearTimeout(pending.timer);
    }
    pendingTransactions.delete(transactionId);
    updateConnectionControls();
    throw error;
  }
}

function clearData() {
  points.length = 0;
  capturedPoints.length = 0;
  firstDeviceTimestamp = null;
  latestFrame = null;
  Object.keys(stat).forEach(key => { stat[key] = 0; });
  decoder.reset();
  lastSample.textContent = "\u7b49\u5f85\u6570\u636e";
}

function exportCsv() {
  if (capturedPoints.length === 0) {
    return;
  }
  const rows = [
    "sequence,timestamp_us,setpoint,measurement,control_output,auxiliary," +
    "loop_count,period_us,execution_us,jitter_us,deadline_miss_count,flags"
  ];
  capturedPoints.forEach(point => {
    rows.push([
      point.sequence, point.timestamp, point.setpoint,
      point.measurement, point.output, point.auxiliary,
      point.loop, point.period, point.execution, point.jitter,
      point.deadlineMiss, point.flags
    ].join(","));
  });
  const url = URL.createObjectURL(new Blob(
    [rows.join("\n")], { type: "text/csv;charset=utf-8" }));
  const link = document.createElement("a");
  link.href = url;
  link.download = "echo-telemetry.csv";
  link.click();
  URL.revokeObjectURL(url);
}

connectButton.addEventListener("click", connect);
disconnectButton.addEventListener("click", () => {
  disconnect().catch(error => {
    setConnectionState(error.message, "error");
  });
});
clearButton.addEventListener("click", clearData);
exportButton.addEventListener("click", exportCsv);
sendButton.addEventListener("click", () => {
  sendParameter().catch(error => {
    tuningStatus.textContent = error.message;
  });
});
if ("serial" in navigator) {
  navigator.serial.addEventListener("disconnect", event => {
    const disconnectedPort = event.port || event.target;
    if (disconnectedPort === navigator.serial) {
      return;
    }
    disconnect(disconnectedPort).catch(error => {
      setConnectionState(error.message, "error");
    });
  });
  browserStatus.textContent = "Edge Web Serial";
} else {
  browserStatus.textContent = "\u8bf7\u4f7f\u7528\u65b0\u7248 Edge \u6216 Chrome";
}

createChannelControls();
setInterval(updateStatus, 250);
requestAnimationFrame(drawChart);
