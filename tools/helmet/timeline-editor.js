/* Face Studio timeline UI. Only the local compiler and firmware twin decide validity or pixels. */
(() => {
  "use strict";
  const element = name => document.getElementById("timeline-" + name);
  const regions = {left:"Left eye", right:"Right eye", mouth:"Mouth", full:"Full face"};
  const channels = {x:"Horizontal", y:"Vertical", lid:"Lid closure", squint:"Squint", sprite:"Sprite", aperture:"Aperture"};
  const curves = {linear:"Linear", in:"Ease in", out:"Ease out", "in-out":"Ease in/out", settle:"Settle"};
  let clip, position = 0, selected = -1, playing = false, timer = null;
  let revision = 0, snapshot = 0, importOrder = 0, busy = false, queued = false, batch = [];

  function optionList(name, values) {
    for (const [value, label] of Object.entries(values)) {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = label;
      element(name).append(option);
    }
  }

  async function request(path, payload) {
    const response = await fetch("/api/timeline/" + path, payload === undefined ? undefined : {
      method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify(payload)
    });
    const value = await response.json();
    if (!response.ok) {
      const error = value.error;
      throw Error(typeof error === "string" ? error : [error?.path, error?.message].filter(Boolean).join(" "));
    }
    return value;
  }

  function status(text) { element("status").textContent = text; }
  function badge(text) { element("badge").textContent = text; }
  function duration() { return Math.max(0, Math.min(60000, Number(clip.duration_ms) || 0)); }
  function number(name) { return element(name).value === "" ? null : Number(element(name).value); }

  function renderRuler() {
    const limit = duration();
    element("scrub").max = String(limit);
    element("ruler").replaceChildren(...[0,.25,.5,.75,1].map(fraction => {
      const tick = document.createElement("span");
      tick.textContent = (fraction * limit / 1000).toFixed(2) + " s";
      return tick;
    }));
  }

  function move(at) {
    position = Math.round(Math.min(duration(), Math.max(0, at)));
    element("scrub").value = position;
    element("time").textContent = position + " ms";
  }

  function draw(frame) {
    const canvas = element("face"), context = canvas.getContext("2d");
    const pixels = frame.frame.match(/../g).map(s => parseInt(s, 16));
    context.fillStyle = "#111512";
    context.fillRect(0,0,672,48);
    for (let y=0; y<8; ++y) for (let x=0; x<112; ++x) {
      const lit = !frame.hidden && (pixels[y*14 + (x>>3)] & (128 >> (x&7)));
      context.fillStyle = lit ? "#b6f597" : "#26332a";
      context.fillRect(x*6+1,y*6+1,4,4);
    }
  }

  function pause() {
    playing = false;
    clearTimeout(timer);
    batch = [];
    element("play").textContent = "Play";
  }

  function queueSeek() {
    snapshot += 1;
    queued = true;
    void drain();
  }

  async function drain() {
    if (busy || !queued) return;
    queued = false;
    busy = true;
    const ticket = snapshot, version = revision;
    const locations = [position];
    if (playing) {
      for (let i=1; i<12 && position+i*16<=duration(); ++i) locations.push(position+i*16);
    }
    try {
      const result = await request("seek", {document:clip, positions_ms:locations});
      if (ticket !== snapshot || version !== revision) return;
      const [frame, ...rest] = result.frames;
      draw(frame);
      const p = result.progress;
      status(`Firmware preview · ${p.started} started · ${p.rejected} rejected · ${p.skipped} skipped · ${result.metrics.seek_ms.toFixed(1)} ms replay`);
      if (!playing) return;
      batch = rest;
      tick();
    } catch (error) {
      if (ticket === snapshot) {
        pause();
        status(error.message);
      }
    } finally {
      busy = false;
      if (queued) void drain();
    }
  }

  function tick() {
    timer = setTimeout(() => {
      if (!playing) return;
      if (batch.length) {
        const frame = batch.shift();
        move(frame.at_ms);
        draw(frame);
        tick();
      } else if (position < duration()) {
        move(position + 16);
        queueSeek();
      } else pause();
    }, 16);
  }

  function seek(at) {
    pause();
    move(at);
    queueSeek();
  }

  function resetEditor(value) {
    pause();
    clip = value;
    selected = clip.events.length ? 0 : -1;
    for (const name of ["name","id"]) element(name).value = clip[name];
    element("duration").value = clip.duration_ms;
    element("expression").value = clip.initial.expression;
    element("seed").value = clip.initial.cosmetic_seed;
    element("audio").value = clip.initial.audio;
    element("blink").checked = clip.initial.natural_blinks;
    element("idle").checked = clip.initial.idle_motion;
    renderRuler();
    choose(selected);
    move(0);
    change();
  }

  function change() {
    pause();
    revision += 1;
    badge("Unsaved changes");
    queueSeek();
  }

  function choose(index) {
    selected = index;
    element("inspector").disabled = index < 0;
    element("delete").disabled = index < 0;
    const item = clip.events[index];
    if (item) {
      for (const [control, key] of Object.entries(eventFields)) element(control).value = item[key];
      element("repeat").value = item.repeat?.count ?? 1;
      element("period").value = item.repeat?.period_ms ?? 100;
      element("period").disabled = !item.repeat;
    }
    renderEvents();
  }

  function renderEvents() {
    const container = element("events");
    container.replaceChildren();
    for (const [region, name] of Object.entries(regions)) {
      for (const [channel, label] of Object.entries(channels)) {
        const group = clip.events.map((event, index) => ({event,index})).filter(({event}) =>
          event.region === region && event.channel === channel).sort((a,b) => a.event.at_ms-b.event.at_ms);
        if (!group.length) continue;
        const heading = document.createElement("strong");
        heading.textContent = name + " · " + label;
        container.append(heading);
        for (const {event,index} of group) {
          const button = document.createElement("button");
          button.type = "button";
          button.textContent = `${event.at_ms} ms · ${event.from} → ${event.to} · priority ${event.priority}`;
          button.setAttribute("aria-pressed", String(index === selected));
          button.addEventListener("click", () => { choose(index); seek(event.at_ms); });
          container.append(button);
        }
      }
    }
  }

  async function compiled(kind) {
    pause();
    const version = revision;
    try {
      const value = await request("compile", {document:clip});
      if (revision !== version) return;
      if (!kind) {
        badge("Validated");
        status("Valid timeline · " + value.asset_bytes + " compiled bytes");
        return;
      }
      const url = URL.createObjectURL(new Blob([value[kind]], {type:kind === "json" ? "application/json" : "text/plain"}));
      const link = document.createElement("a");
      link.href = url;
      link.download = kind === "json" ? value.document.id + ".json" : "TimelineAsset.h";
      link.click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
      badge(kind === "json" ? "Source exported" : "Compiled asset exported");
      status(value.asset_bytes + " bytes · format version " + value.document.version);
    } catch (error) {
      if (revision === version) status(error.message);
    }
  }

  const eventFields = {region:"region", channel:"channel", at:"at_ms", length:"duration_ms",
                       from:"from", to:"to", easing:"easing", priority:"priority"};

  async function init() {
    const response = await fetch("/api/timeline/sample");
    if (!response.ok) return; // Hardware controls have no timeline interface.
    optionList("region", regions);
    optionList("channel", channels);
    optionList("easing", curves);
    optionList("expression", {0:"Happy",1:"Surprised",2:"Angry",3:"vwv",4:"Sleepy",5:"Wink"});
    element("play").onclick = () => {
      if (playing) { pause(); snapshot += 1; return; }
      if (position >= duration()) move(0);
      playing = true;
      element("play").textContent = "Pause";
      queueSeek();
    };
    element("restart").onclick = () => seek(0);
    element("step").onclick = () => seek(position+16);
    element("scrub").oninput = () => seek(Number(element("scrub").value));
    element("add").onclick = () => {
      if (clip.events.length >= 32) { status("The clip already has 32 events."); return; }
      clip.events.push({at_ms:position,duration_ms:Math.min(160,duration()-position),
                        region:"left",channel:"x",from:0,to:1,easing:"in-out",priority:3});
      choose(clip.events.length-1);
      change();
    };
    element("delete").onclick = () => {
      if (selected < 0) return;
      clip.events.splice(selected,1);
      choose(Math.min(selected,clip.events.length-1));
      change();
    };
    for (const name of ["name","id","duration","expression","seed","audio","blink","idle"]) {
      element(name).oninput = e => {
        if (name === "name" || name === "id") clip[name] = e.target.value;
        else if (name === "duration") { clip.duration_ms = number(name); renderRuler(); move(position); }
        else if (name === "blink" || name === "idle") clip.initial[name === "blink" ? "natural_blinks" : "idle_motion"] = e.target.checked;
        else clip.initial[name === "seed" ? "cosmetic_seed" : name] = number(name);
        change();
      };
    }
    for (const [control,key] of Object.entries(eventFields)) {
      element(control).oninput = e => {
        if (selected < 0) return;
        clip.events[selected][key] = ["region","channel","easing"].includes(control) ? e.target.value : number(control);
        renderEvents();
        change();
      };
    }
    for (const name of ["repeat","period"]) element(name).oninput = () => {
      if (selected < 0) return;
      if (number("repeat") === 1) delete clip.events[selected].repeat;
      else clip.events[selected].repeat = {count:number("repeat"),period_ms:number("period")};
      element("period").disabled = !clip.events[selected].repeat;
      change();
    };
    element("validate").onclick = () => compiled();
    element("json").onclick = () => compiled("json");
    element("header").onclick = () => compiled("header");
    element("import").onclick = () => element("file").click();
    element("file").onchange = async e => {
      const file = e.target.files[0];
      e.target.value = "";
      if (!file) return;
      pause();
      snapshot += 1;
      queued = false;
      const version = revision, order = ++importOrder;
      try {
        if (file.size > 30000) throw Error("Timeline JSON exceeds the import limit.");
        const value = await request("compile", {document_json:await file.text()});
        if (version !== revision || order !== importOrder) return;
        resetEditor(value.document);
        badge("Validated import");
      } catch (error) { if (version === revision && order === importOrder) status(error.message); }
    };
    document.getElementById("timeline").addEventListener("keydown", e => {
      if (e.target.matches("input,select,button,textarea") || e.ctrlKey || e.metaKey || e.altKey) return;
      if (e.key === " ") { e.preventDefault(); element("play").click(); }
      if (e.key === "ArrowRight" || e.key === "ArrowLeft") {
        e.preventDefault();
        seek(position + (e.key === "ArrowRight" ? 16 : -16));
      }
      if (e.key === "Home") { e.preventDefault(); seek(0); }
    });
    document.getElementById("timeline").hidden = false;
    resetEditor(await response.json());
    badge("Sample clip");
  }

  init().catch(error => status(error.message));
})();
