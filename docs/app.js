(() => {
  const apkDrop = document.getElementById('apk-drop');
  const apkInput = document.getElementById('apk-input');
  const apkName = document.getElementById('apk-name');
  const buildBtn = document.getElementById('build-btn');
  const progressBox = document.getElementById('progress');
  const bar = document.getElementById('bar');
  const status = document.getElementById('status');
  const logBox = document.getElementById('log');
  const hashWarning = document.getElementById('hash-warning');

  const EXPECTED_APK_SHA256 =
    '0f27a69193d79b79f8b878df7b75dcd7ae4df456ba5fec03ec3d3d4eca579ad9';
  const BASE_VPK_NAME = 'superhexagon_vita.vpk';
  const DATA_ROOT = 'superhexagon/';
  const APK_ICON = 'res/eQ.png';
  const APK_LOADING_3X = 'assets/images/loading@3x.png';
  const LIVEAREA_STARTUP = 'livearea-startup.png';
  const LIVEAREA_BG0 = 'livearea-bg0.png';
  const OUTPUT_AUDIO_RATE = 48000;
  const REQUIRED_LIBS = [
    'lib/armeabi-v7a/libc++_shared.so',
    'lib/armeabi-v7a/liboboe.so',
    'lib/armeabi-v7a/libopenFrameworksAndroid.so',
    'lib/armeabi-v7a/libsuperhexagon.so',
  ];

  const state = {
    apk: null,
    apkHashOk: null,
  };

  const fmtBytes = (n) => {
    if (n < 1024) return `${n} B`;
    const units = ['KB', 'MB', 'GB'];
    let v = n / 1024;
    let i = 0;
    while (v >= 1024 && i < units.length - 1) { v /= 1024; i++; }
    return `${v.toFixed(v >= 10 ? 1 : 2)} ${units[i]}`;
  };

  const setProgress = (pct, msg) => {
    progressBox.hidden = false;
    bar.style.setProperty('--pct', `${Math.max(0, Math.min(100, pct))}%`);
    status.textContent = msg;
  };

  const log = (msg, cls) => {
    logBox.hidden = false;
    const line = document.createElement('div');
    if (cls) line.className = cls;
    line.textContent = msg;
    logBox.appendChild(line);
    logBox.scrollTop = logBox.scrollHeight;
  };

  const refreshButton = () => {
    buildBtn.disabled = !state.apk;
  };

  const bytesToHex = (buf) =>
    [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, '0')).join('');

  const readFile = (file, onProgress) =>
    new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onerror = () => reject(reader.error || new Error('failed to read file'));
      reader.onprogress = (e) => {
        if (onProgress && e.lengthComputable) onProgress(e.loaded / e.total);
      };
      reader.onload = () => resolve(new Uint8Array(reader.result));
      reader.readAsArrayBuffer(file);
    });

  const sha256HexOfFile = async (file) => {
    const data = await readFile(file);
    const digest = await crypto.subtle.digest('SHA-256', data);
    return bytesToHex(digest);
  };

  const updateHashWarning = () => {
    hashWarning.hidden = !(state.apk && state.apkHashOk === false);
  };

  const handleFile = (file) => {
    if (!file) return;
    state.apk = file;
    state.apkHashOk = null;
    apkName.textContent = `${file.name} (${fmtBytes(file.size)})`;
    apkDrop.classList.add('ready');
    refreshButton();
    updateHashWarning();

    if (crypto.subtle) {
      sha256HexOfFile(file).then((hex) => {
        if (state.apk !== file) return;
        state.apkHashOk = hex === EXPECTED_APK_SHA256;
        log(
          state.apkHashOk ? 'APK checksum matches Humble v2.7.7.' : `APK checksum differs: ${hex}`,
          state.apkHashOk ? 'ok' : 'warn'
        );
        updateHashWarning();
      }).catch(() => {
        state.apkHashOk = null;
      });
    }
  };

  const bindDrop = () => {
    apkDrop.addEventListener('click', () => apkInput.click());
    apkDrop.addEventListener('dragover', (e) => {
      e.preventDefault();
      apkDrop.classList.add('drag');
    });
    apkDrop.addEventListener('dragleave', () => apkDrop.classList.remove('drag'));
    apkDrop.addEventListener('drop', (e) => {
      e.preventDefault();
      apkDrop.classList.remove('drag');
      handleFile(e.dataTransfer.files?.[0]);
    });
    apkInput.addEventListener('change', () => handleFile(apkInput.files?.[0]));
  };

  const unzipAsync = (data) =>
    new Promise((resolve, reject) => {
      fflate.unzip(data, (err, files) => err ? reject(err) : resolve(files));
    });

  const zipAsync = (files, opts) =>
    new Promise((resolve, reject) => {
      fflate.zip(files, opts, (err, out) => err ? reject(err) : resolve(out));
    });

  const fitRect = (srcW, srcH, dstW, dstH, mode) => {
    const scale = mode === 'cover'
      ? Math.max(dstW / srcW, dstH / srcH)
      : Math.min(dstW / srcW, dstH / srcH);
    const w = srcW * scale;
    const h = srcH * scale;
    return [(dstW - w) / 2, (dstH - h) / 2, w, h];
  };

  const derivePng = async (sourceBytes, width, height, opts = {}) => {
    const blob = new Blob([sourceBytes], { type: 'image/png' });
    const bitmap = await createImageBitmap(blob);
    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    ctx.clearRect(0, 0, width, height);
    if (opts.background) {
      ctx.fillStyle = opts.background;
      ctx.fillRect(0, 0, width, height);
    }
    const [dx, dy, dw, dh] = fitRect(bitmap.width, bitmap.height, width, height, opts.fit || 'contain');
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(bitmap, dx, dy, dw, dh);
    const imgData = ctx.getImageData(0, 0, width, height);
    const rgba = imgData.data;
    if (opts.noAlpha) {
      for (let i = 3; i < rgba.length; i += 4) rgba[i] = 255;
    }
    const buf = new Uint8Array(rgba.length);
    buf.set(rgba);
    return new Uint8Array(UPNG.encode([buf.buffer], width, height, 256));
  };

  const yieldToUi = () => new Promise((resolve) => setTimeout(resolve, 0));

  const patchSettings = (bytes) => {
    const decoder = new TextDecoder('utf-8');
    const encoder = new TextEncoder();
    let xml = decoder.decode(bytes);
    xml = xml
      .replace(/<highframerate>.*?<\/highframerate>/s, '<highframerate>0</highframerate>')
      .replace(/<framerate_limit>.*?<\/framerate_limit>/s, '<framerate_limit>60</framerate_limit>');
    if (!xml.includes('<highframerate>')) {
      xml = xml.replace('</SETTINGS>', '    <highframerate>0</highframerate>\n</SETTINGS>');
    }
    if (!xml.includes('<framerate_limit>')) {
      xml = xml.replace('</SETTINGS>', '    <framerate_limit>60</framerate_limit>\n</SETTINGS>');
    }
    return encoder.encode(xml);
  };

  const readAscii = (bytes, off, len) =>
    String.fromCharCode(...bytes.subarray(off, off + len));

  const parsePcmWav = (bytes) => {
    if (bytes.length < 44 || readAscii(bytes, 0, 4) !== 'RIFF' || readAscii(bytes, 8, 4) !== 'WAVE') {
      throw new Error('expected RIFF/WAVE PCM data');
    }

    const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    let pos = 12;
    let fmt = null;
    let dataOff = -1;
    let dataSize = 0;

    while (pos + 8 <= bytes.length) {
      const tag = readAscii(bytes, pos, 4);
      const size = dv.getUint32(pos + 4, true);
      const body = pos + 8;
      if (body + size > bytes.length) break;

      if (tag === 'fmt ') {
        fmt = {
          audioFormat: dv.getUint16(body, true),
          channels: dv.getUint16(body + 2, true),
          rate: dv.getUint32(body + 4, true),
          bits: dv.getUint16(body + 14, true),
        };
      } else if (tag === 'data') {
        dataOff = body;
        dataSize = size;
      }

      pos = body + size + (size & 1);
    }

    if (!fmt || dataOff < 0 || fmt.audioFormat !== 1 || fmt.bits !== 16 ||
        (fmt.channels !== 1 && fmt.channels !== 2)) {
      throw new Error('expected 16-bit mono/stereo PCM WAV');
    }

    return {
      channels: fmt.channels,
      rate: fmt.rate,
      frames: Math.floor(dataSize / (fmt.channels * 2)),
      pcm: new Int16Array(bytes.buffer, bytes.byteOffset + dataOff, Math.floor(dataSize / 2)),
    };
  };

  const writePcmWav = (samples, channels, rate) => {
    const dataBytes = samples.length * 2;
    const out = new Uint8Array(44 + dataBytes);
    const dv = new DataView(out.buffer);
    const putAscii = (off, s) => {
      for (let i = 0; i < s.length; i++) out[off + i] = s.charCodeAt(i);
    };

    putAscii(0, 'RIFF');
    dv.setUint32(4, 36 + dataBytes, true);
    putAscii(8, 'WAVE');
    putAscii(12, 'fmt ');
    dv.setUint32(16, 16, true);
    dv.setUint16(20, 1, true);
    dv.setUint16(22, channels, true);
    dv.setUint32(24, rate, true);
    dv.setUint32(28, rate * channels * 2, true);
    dv.setUint16(32, channels * 2, true);
    dv.setUint16(34, 16, true);
    putAscii(36, 'data');
    dv.setUint32(40, dataBytes, true);
    new Int16Array(out.buffer, 44, samples.length).set(samples);
    return out;
  };

  const convertPcmWav48kStereo = async (bytes, label) => {
    const wav = parsePcmWav(bytes);
    if (wav.rate === OUTPUT_AUDIO_RATE && wav.channels === 2) return bytes;

    const outFrames = Math.max(1, Math.round(wav.frames * OUTPUT_AUDIO_RATE / wav.rate));
    const out = new Int16Array(outFrames * 2);

    for (let i = 0; i < outFrames; i++) {
      const src = i * wav.rate / OUTPUT_AUDIO_RATE;
      const idx = Math.floor(src);
      const frac = src - idx;
      const next = Math.min(idx + 1, wav.frames - 1);
      const cur = Math.min(idx, wav.frames - 1);

      for (let ch = 0; ch < 2; ch++) {
        const srcCh = wav.channels === 1 ? 0 : ch;
        const s0 = wav.pcm[cur * wav.channels + srcCh];
        const s1 = wav.pcm[next * wav.channels + srcCh];
        out[i * 2 + ch] = Math.round(s0 + (s1 - s0) * frac);
      }

      if ((i & 0x3ffff) === 0) {
        setProgress(58 + Math.min(16, (i / outFrames) * 16), `Converting audio ${label}...`);
        await yieldToUi();
      }
    }

    return writePcmWav(out, 2, OUTPUT_AUDIO_RATE);
  };

  const basename = (path) => path.substring(path.lastIndexOf('/') + 1);

  const packStored = (set) => {
    const input = {};
    for (const k of Object.keys(set).sort()) {
      input[k] = [set[k], { level: 0 }];
    }
    return zipAsync(input, { level: 0 });
  };

  const fetchBaseVpk = async () => {
    const resp = await fetch(BASE_VPK_NAME, { cache: 'reload' });
    if (!resp.ok) throw new Error(`Failed to fetch ${BASE_VPK_NAME} (${resp.status})`);
    return new Uint8Array(await resp.arrayBuffer());
  };

  const fetchBytes = async (url) => {
    const resp = await fetch(url, { cache: 'reload' });
    if (!resp.ok) throw new Error(`Failed to fetch ${url} (${resp.status})`);
    return new Uint8Array(await resp.arrayBuffer());
  };

  const build = async () => {
    buildBtn.disabled = true;
    logBox.innerHTML = '';

    try {
      const apk = state.apk;

      setProgress(4, `Reading APK (${fmtBytes(apk.size)})...`);
      const apkBytes = await readFile(apk, (p) =>
        setProgress(4 + p * 20, `Reading APK... ${(p * 100).toFixed(0)}%`)
      );
      log(`Read APK: ${fmtBytes(apkBytes.length)}`, 'ok');

      setProgress(25, 'Fetching base VPK...');
      const baseVpk = await fetchBaseVpk();
      log(`Fetched base VPK: ${fmtBytes(baseVpk.length)}`, 'ok');

      setProgress(30, 'Decompressing base VPK...');
      const vpkFiles = await unzipAsync(baseVpk);
      log(`Base VPK entries: ${Object.keys(vpkFiles).length}`);

      setProgress(38, 'Decompressing APK...');
      const apkFiles = await unzipAsync(apkBytes);
      log(`APK entries: ${Object.keys(apkFiles).length}`);

      const vpkEntries = Object.create(null);
      const dataEntries = Object.create(null);

      for (const [name, data] of Object.entries(vpkFiles)) {
        vpkEntries[name] = data;
      }

      setProgress(48, 'Collecting Android libraries...');
      for (const lib of REQUIRED_LIBS) {
        const data = apkFiles[lib];
        if (!data) {
          throw new Error(`Missing ${lib}. This port needs the 32-bit ARM APK.`);
        }
        dataEntries[DATA_ROOT + basename(lib)] = data;
      }
      log(`Android libraries: ${REQUIRED_LIBS.length}`, 'ok');

      setProgress(56, 'Collecting and converting game assets...');
      let assetCount = 0;
      let convertedAudio = 0;
      let patchedSettings = false;
      for (const [name, data] of Object.entries(apkFiles)) {
        if (!name.startsWith('assets/')) continue;
        let outData = data;
        if (name === 'assets/settings.dat') {
          outData = patchSettings(data);
          patchedSettings = true;
        } else if (/^assets\/(music\/music\d+\.dat|sounds\/.+\.wav)$/.test(name)) {
          outData = await convertPcmWav48kStereo(data, name.substring('assets/'.length));
          convertedAudio++;
        }
        dataEntries[DATA_ROOT + name] = outData;
        assetCount++;
      }
      if (!assetCount) throw new Error('No assets/ entries found in APK.');
      if (!patchedSettings) throw new Error('Missing assets/settings.dat.');
      log(`Assets copied: ${assetCount}; audio converted to 48 kHz stereo: ${convertedAudio}; settings patched for 60 FPS.`, 'ok');

      setProgress(74, 'Building LiveArea assets...');
      const iconSource = apkFiles[APK_ICON];
      if (!iconSource) throw new Error(`Missing APK icon source ${APK_ICON}.`);
      vpkEntries['sce_sys/icon0.png'] = await derivePng(iconSource, 128, 128, {
        fit: 'contain',
        noAlpha: true,
      });
      const loadingSource = apkFiles[APK_LOADING_3X];
      if (!loadingSource) throw new Error(`Missing APK loading source ${APK_LOADING_3X}.`);
      vpkEntries['sce_sys/pic0.png'] = await derivePng(loadingSource, 960, 544, {
        fit: 'contain',
        background: '#000000',
        noAlpha: true,
      });
      vpkEntries['sce_sys/livearea/contents/startup.png'] = await derivePng(
        await fetchBytes(LIVEAREA_STARTUP),
        280,
        158,
        { fit: 'contain', background: '#000000', noAlpha: true }
      );
      vpkEntries['sce_sys/livearea/contents/bg0.png'] = await derivePng(
        await fetchBytes(LIVEAREA_BG0),
        840,
        500,
        { fit: 'cover', noAlpha: true }
      );
      log('LiveArea icon and pic0 derived from APK; startup and background loaded from site assets.', 'ok');

      for (const required of ['eboot.bin', 'sce_sys/param.sfo']) {
        if (!vpkEntries[required]) throw new Error(`Missing base VPK entry: ${required}`);
      }
      for (const required of [
        'superhexagon/libsuperhexagon.so',
        'superhexagon/libopenFrameworksAndroid.so',
        'superhexagon/liboboe.so',
        'superhexagon/libc++_shared.so',
        'superhexagon/assets/settings.dat',
      ]) {
        if (!dataEntries[required]) throw new Error(`Missing data entry: ${required}`);
      }

      setProgress(82, `Packing VPK (${Object.keys(vpkEntries).length} entries)...`);
      const vpkBytes = await packStored(vpkEntries);
      log(`Built VPK: ${fmtBytes(vpkBytes.length)}`, 'ok');

      setProgress(93, 'Bundling VPK and data folder...');
      fetch('https://counters.mcallbos.co/v1/hit/superhexagon-vpk', {
        mode: 'no-cors',
        keepalive: true,
      }).catch(() => {});

      const bundleBytes = await packStored({
        [BASE_VPK_NAME]: vpkBytes,
        ...dataEntries,
      });

      const blob = new Blob([bundleBytes], { type: 'application/zip' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'superhexagon.zip';
      document.body.appendChild(a);
      a.click();
      a.remove();
      URL.revokeObjectURL(url);

      setProgress(100, `Done. Bundle ${fmtBytes(bundleBytes.length)}.`);
      log(`All done. Install ${BASE_VPK_NAME} and copy ${DATA_ROOT} to ux0:data/.`, 'ok');
    } catch (err) {
      console.error(err);
      setProgress(100, 'Build failed.');
      log(err && err.message ? err.message : String(err), 'err');
    } finally {
      refreshButton();
    }
  };

  bindDrop();
  buildBtn.addEventListener('click', build);
  refreshButton();
})();
