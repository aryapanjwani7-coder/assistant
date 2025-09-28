/********************
 * Bootstrapping    *
 ********************/
window.addEventListener('DOMContentLoaded', ()=>{ lucide.createIcons(); });

const $ = (s)=>document.querySelector(s);
const $$ = (s)=>document.querySelectorAll(s);
const ESC = (s)=> (s||'').replace(/[&<>"']/g, m=>({ '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;', "'":'&#39;' }[m]));

/********************
 * Persistent store *
 ********************/
const store = {
  get key(){ return localStorage.getItem('OPENAI_KEY') || ''; },
  set key(v){ localStorage.setItem('OPENAI_KEY', v); },

  get model(){ return localStorage.getItem('OPENAI_MODEL') || 'gpt-4o-mini'; },
  set model(v){ localStorage.setItem('OPENAI_MODEL', v); },

  get chatHistory(){ return JSON.parse(localStorage.getItem('CHAT_HISTORY') || '[]'); },
  set chatHistory(v){ localStorage.setItem('CHAT_HISTORY', JSON.stringify(v)); },

  // Per-day plans: { "YYYY-MM-DD": { day, items:[...], meta: {...} } }
  get plans(){ return JSON.parse(localStorage.getItem('PLANS_BY_DAY') || '{}'); },
  set plans(v){ localStorage.setItem('PLANS_BY_DAY', JSON.stringify(v)); },

  get planDay(){ return localStorage.getItem('PLAN_DAY') || dayjs().format('YYYY-MM-DD'); },
  set planDay(v){ localStorage.setItem('PLAN_DAY', v); },

  get settings(){ return JSON.parse(localStorage.getItem('PLAN_SETTINGS') || '{"earliest":"08:00","latest":"23:30","minGap":10,"travelSame":10,"travelDiff":20,"homeBase":""}'); },
  set settings(v){ localStorage.setItem('PLAN_SETTINGS', JSON.stringify(v)); },
};

function getPlan(day){ return store.plans[day] || null; }
function setPlan(day, plan){ const all = store.plans; all[day] = plan; store.plans = all; }

/*****************
 * Tabs & header *
 *****************/
(function initTabs(){
  const tabs = $$('.tab');
  const sections = ['chat','plan','flows'];
  tabs.forEach(btn=>{
    btn.addEventListener('click', ()=>{
      sections.forEach(id=>$('#'+id).classList.add('hidden'));
      $('#'+btn.dataset.tab).classList.remove('hidden');
      tabs.forEach(b=>b.classList.remove('bg-blue-600','text-white'));
      btn.classList.add('bg-blue-600','text-white');
      lucide.createIcons();
    });
  });
  document.querySelector('[data-tab="plan"]').click();

  // API Key & model
  $('#apiKey').value = store.key;
  $('#modelSelect').value = store.model;
  $('#saveKey').onclick = ()=>{
    store.key = $('#apiKey').value.trim();
    store.model = $('#modelSelect').value;
    alert('Saved API key & model!');
  };
  $('#clearKey').onclick = ()=>{
    localStorage.removeItem('OPENAI_KEY'); $('#apiKey').value='';
    alert('Cleared key.');
  };

  // Notifications
  $('#notifyPerm').onclick = async ()=>{ const s = await Notification.requestPermission(); alert('Notification permission: ' + s); };
})();

/********
 * Chat *
 ********/
(function initChat(){
  function renderChat(){
    const log = $('#chatLog'); if(!log) return;
    log.innerHTML = '';
    for(const m of store.chatHistory){
      const me = m.role==='user';
      const bubble = document.createElement('div');
      bubble.className = 'p-3 rounded-xl border ' + (me ? 'bg-indigo-50 border-indigo-100' : 'bg-white border-slate-100');
      bubble.innerHTML = '<div class="text-[11px] uppercase tracking-wide text-slate-500 mb-1">'+(me?'You':'Assistant')+'</div><div class="whitespace-pre-wrap">'+ESC(m.content)+'</div>';
      log.appendChild(bubble);
    }
    log.scrollTop = log.scrollHeight;
  }
  renderChat();

  $('#sendChat')?.addEventListener('click', async ()=>{
    const t = $('#chatInput').value.trim(); if(!t) return;
    const hist = store.chatHistory; hist.push({role:'user', content:t}); store.chatHistory = hist; renderChat(); $('#chatInput').value='';
    try{
      const answer = await askOpenAI([{role:'system', content:'Be helpful and concise.'}, ...store.chatHistory]);
      hist.push({role:'assistant', content:answer}); store.chatHistory = hist; renderChat();
    }catch(e){ alert('Chat error: '+e.message); }
  });
})();

/*****************
 * OpenAI helper *
 *****************/
const OPENAI_URL = 'https://api.openai.com/v1/chat/completions';
async function askOpenAI(messages, {temperature=0.2, json=false} = {}){
  const key = store.key;
  if(!key) throw new Error('Missing OpenAI API key. Click Save at the top.');
  const body = { model: store.model, temperature, messages };
  if(json) body.response_format = { type: 'json_object' };
  const res = await fetch(OPENAI_URL, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'Authorization': 'Bearer ' + key },
    body: JSON.stringify(body)
  });
  if(!res.ok){ throw new Error(await res.text()); }
  const data = await res.json();
  return data.choices?.[0]?.message?.content?.trim() || '';
}

/************
 * Planner  *
 ************/
const startHr = 6, endHr = 24;

function emptyPlan(dayIso){ return { day: dayIso, items: [], meta: { clarifications: [] } }; }

// Normalize ranges like "9-9:50am" → "9am to 9:50am"
function normalizeTimeRange(line) {
  let s = line;
  s = s.replace(/from\s*(\d{1,2}(?::\d{2})?)\s*[-–]\s*(\d{1,2}(?::\d{2})?)\s*(am|pm)\b/i,
                (m, t1, t2, ap) => `from ${t1}${ap} to ${t2}${ap}`);
  s = s.replace(/(\d{1,2}(?::\d{2})?)\s*[-–]\s*(\d{1,2}(?::\d{2})?)\s*(am|pm)\b/i,
                (m, t1, t2, ap) => `${t1}${ap} to ${t2}${ap}`);
  s = s.replace(/from\s*(\d{1,2}(?::\d{2})?)\s*(?:to|–|-)\s*(\d{1,2}(?::\d{2})?)\s*(am|pm)\b/i,
                (m, t1, t2, ap) => `from ${t1}${ap} to ${t2}${ap}`);
  s = s.replace(/(\d{1,2}(?::\d{2})?)\s*(?:to|–|-)\s*(\d{1,2}(?::\d{2})?)\s*(am|pm)\b/i,
                (m, t1, t2, ap) => `${t1}${ap} to ${t2}${ap}`);
  return s;
}

/* ---------- Local fallback (if OpenAI fails) ---------- */
function localDraft(text){
  const lines = text.split(/\n+|[.;]\s+/).map(s=>normalizeTimeRange(s.trim())).filter(Boolean);
  const items = [];
  const defaultDur = (title)=>{
    const t = title.toLowerCase();
    if(t.includes('breakfast')) return 30;
    if(t.includes('lunch')) return 45;
    if(t.includes('dinner')) return 45;
    if(t.includes('meditat')) return 20;
    if(t.includes('nail')) return 10;
    return 30;
  };
  const getLocation = (s)=> {
    const m = s.match(/\bat\s+([^,.;]+)\b/i);
    return m ? m[1].trim() : '';
  }
  for(const raw of lines){
    const line = raw;
    const durMatch = line.match(/(\d+)\s*h(?:\s*(\d+)\s*m)?|(\d+)\s*m/i);
    const durMin = durMatch ? (durMatch[3] ? parseInt(durMatch[3]) : (parseInt(durMatch[1]||0)*60 + parseInt(durMatch[2]||0))) : null;

    function titleWithCompanion(){
      const m = line.match(/(.+?)(\swith\s+[^,.;]+)(.*)$/i);
      const strip = (txt)=>txt.replace(/\b(tomorrow|today|at|from|until|till|to|around|about|ish)\b/gi,'')
                              .replace(/\d{1,2}\s*[:.]?\d{0,2}\s*(am|pm)?/gi,'')
                              .replace(/\b(\d+)\s*h(\s*\d+\s*m)?\b/gi,'')
                              .replace(/\b(\d+)\s*m\b/gi,'')
                              .replace(/\s{2,}/g,' ').trim().replace(/^[-–—,:]+/,'').trim();
      if(m){ const left = strip(m[1]); const base = left || 'Task'; return `${base} ${m[2].trim()}`.trim(); }
      return strip(line) || 'Task';
    }
    const parsed = chrono.parse(line, new Date());
    const location = getLocation(line);

    if(parsed.length && parsed[0].start && parsed[0].end){
      const s = parsed[0].start.date(), e = parsed[0].end.date();
      items.push({ title:titleWithCompanion(), type:'fixed', location,
        start:dayjs(s).format('HH:mm'), end:dayjs(e).format('HH:mm'),
        duration_min: dayjs(e).diff(dayjs(s),'minute'), notes:'' });
      continue;
    }
    if(parsed.length && parsed[0].start){
      const s = parsed[0].start.date(); const d = durMin || defaultDur(line);
      items.push({ title:titleWithCompanion(), type:'fixed', location,
        start:dayjs(s).format('HH:mm'), end:dayjs(s).add(d,'minute').format('HH:mm'),
        duration_min:d, notes:'' });
      continue;
    }

    // wake/sleep markers with no time → collect as context, not schedule
    if(/\bwake\s*up\b/i.test(line) && !durMin && !parsed.length){
      items.push({ title:'Wake up', type:'marker', start:'', end:'', duration_min:0, notes:'' });
      continue;
    }
    if(/\bsleep\b/i.test(line) && /by/i.test(line) && !parsed.length){
      items.push({ title:'Sleep', type:'rest', start:'', end:'23:00', duration_min:0, notes:'' });
      continue;
    }

    const d = durMin || defaultDur(line);
    const isMeal = /\b(breakfast|lunch|dinner)\b/i.test(line);
    items.push({ title:titleWithCompanion(), type: isMeal ? 'meal' : 'flexible', location, start:'', end:'', duration_min:d, notes:'' });
  }
  return { items };
}

/* ---------- AI-first parsing with strict JSON ---------- */
async function aiDraft(text, baseDayIso){
  const sys = `
You are a scheduling parser. Convert natural text into STRICT JSON:
{
  "day": "YYYY-MM-DD",
  "items": [
    {
      "title": "Breakfast with Kobe",
      "type": "fixed" | "flexible" | "meal" | "rest" | "marker",
      "start": "HH:mm",
      "end":   "HH:mm",
      "duration_min": 0,
      "location": "",
      "notes": ""
    }
  ]
}
Rules:
- Assume date ${baseDayIso} unless another date is clearly stated.
- Understand sloppy ranges like "9-9:50am" (fill missing am/pm).
- Extract companions/locations: keep "with X" in title; if "at Y" put Y in "location".
- Meals without times default durations: breakfast 30m, lunch 45m, dinner 45m (mark type "meal").
- "Sleep by 11" -> rest item with end:"23:00" if no explicit time; if "sleep 23:30–07:00" provide both times.
- "Wake up" can be type "marker" if no time given; if time is given, treat as fixed (0–5m).
- Chores with duration but no time -> flexible with duration_min.
- Output MUST be valid JSON. No commentary.`;

  const fewshotUser = `Examples:
- "tomorrow I'll have breakfast from 9-9:50am with Kobe at De Neve"
- "physics 9–11 in Boelter 3400; lunch with Maya ~1 for 45m; meditate; cut nails (10m); sleep by 11; wake up"`;

  const fewshotAssistant = JSON.stringify({
    day: baseDayIso,
    items: [
      { title: "Breakfast with Kobe", type: "meal", start: "09:00", end: "09:50", duration_min: 50, location: "De Neve", notes: "" },
      { title: "Physics (Boelter 3400)", type: "fixed", start: "09:00", end: "11:00", duration_min: 120, location: "", notes: "" },
      { title: "Lunch with Maya", type: "meal", start: "13:00", end: "13:45", duration_min: 45, location: "", notes: "" },
      { title: "Meditate", type: "flexible", start: "", end: "", duration_min: 20, location: "", notes: "" },
      { title: "Cut nails", type: "flexible", start: "", end: "", duration_min: 10, location: "", notes: "" },
      { title: "Sleep", type: "rest", start: "", end: "23:00", duration_min: 0, location: "", notes: "" },
      { title: "Wake up", type: "marker", start: "", end: "", duration_min: 0, location: "", notes: "" }
    ]
  });

  const user = `User text:\n"""${text}"""`;

  const out = await askOpenAI(
    [
      { role:'system', content: sys },
      { role:'user', content: fewshotUser },
      { role:'assistant', content: fewshotAssistant },
      { role:'user', content: user }
    ],
    { temperature: 0.1, json: true }
  );
  return JSON.parse(out);
}

/* ---------- Deduplication when updating plan ---------- */
function canonicalTitle(s){
  return (s||'')
    .toLowerCase()
    .replace(/\s+/g,' ')
    .replace(/[.,;:!?]+/g,'')
    .trim();
}
function timesKey(s,e){ return (s&&e) ? `${s}-${e}` : ''; }

function dedupeMergeItems(existingItems, newItems){
  const merged = [...existingItems];
  const seen = new Map(); // key = title|timesKey -> idx

  // seed with existing
  for(let i=0;i<merged.length;i++){
    const k = canonicalTitle(merged[i].title)+'|'+timesKey(merged[i].start, merged[i].end);
    seen.set(k, i);
  }

  for(const it of newItems){
    const key = canonicalTitle(it.title)+'|'+timesKey(it.start, it.end);
    if(seen.has(key)) {
      // merge notes/location if new one has more
      const idx = seen.get(key);
      if((it.notes||'').length > (merged[idx].notes||'').length) merged[idx].notes = it.notes;
      if((it.location||'').length > (merged[idx].location||'').length) merged[idx].location = it.location;
      continue; // skip duplicate
    }

    // for flexible duplicates with no times: avoid exact title duplicate
    if(!it.start && !it.end){
      const dupIdx = merged.findIndex(x => !x.start && !x.end && canonicalTitle(x.title)===canonicalTitle(it.title));
      if(dupIdx>=0){
        // keep longer duration / richer notes
        if((it.duration_min||0) > (merged[dupIdx].duration_min||0)) merged[dupIdx].duration_min = it.duration_min;
        if((it.notes||'').length > (merged[dupIdx].notes||'').length) merged[dupIdx].notes = it.notes;
        continue;
      }
    }

    seen.set(key, merged.length);
    merged.push(it);
  }

  return merged;
}

/* ---------- Compute context (wake, sleep) & settings ---------- */
function deriveSettingsFromItems(items, baseSettings){
  const s = {...baseSettings};
  // look for wake up explicit time
  const wake = items.find(x=>/wake\s*up/i.test(x.title) && x.start);
  if(wake) s.earliest = wake.start;
  // sleep by
  const sleep = items.find(x=>x.type==='rest' && x.end && !x.start);
  if(sleep) s.latest = sleep.end;

  // clarifications
  const clarifications = [];
  if(!wake && items.some(x=>/wake\s*up/i.test(x.title))) {
    clarifications.push('What time do you want to wake up? (Using '+s.earliest+' for now)');
  }
  if(items.some(x=>/yoga\s*nidra/i.test(x.title)) && !items.some(x=>/home|dorm|apartment|house|dykstra/i.test((x.location||'')+' '+(x.notes||'')))){
    clarifications.push('Do you plan to do Yoga Nidra at home or elsewhere? (affects travel buffer)');
  }

  return {settings: s, clarifications};
}

/* ---------- Allocation with gaps & travel buffers ---------- */
function allocatePlan(baseDayIso, draft, settings){
  const earliest = settings.earliest || '08:00';
  const latest   = settings.latest   || '23:30';
  const minGap   = Number(settings.minGap || 10);
  const travelSame = Number(settings.travelSame || 10);
  const travelDiff = Number(settings.travelDiff || 20);

  const toTime = (hhmm)=>dayjs(baseDayIso + 'T' + hhmm.padStart(5,'0'));
  const startOfDay = toTime(earliest);
  const endOfDay   = toTime(latest);

  // Split items
  const fixed=[], flex=[], markers=[];
  for(const raw of (draft.items||[])){
    const it = {...raw};
    if(it.type==='marker'){ markers.push(it); continue; }

    // treat ANY item with explicit start+end as fixed
    if(it.start && it.end){
      fixed.push({...it, startDT: toTime(it.start), endDT: toTime(it.end)});
      continue;
    }

    // Normalize durations
    let d = Number(it.duration_min)||0;
    if(it.type==='meal' && !d){
      const t = (it.title||'').toLowerCase();
      d = t.includes('breakfast') ? 30 : (t.includes('lunch') || t.includes('dinner')) ? 45 : 30;
    }
    flex.push({...it, duration_min: d || 30});
  }

  // Sort fixed
  fixed.sort((a,b)=>a.startDT - b.startDT);

  // Build gaps considering minGap before/after fixed & travel buffer towards next fixed
  const gaps = [];
  let cursor = startOfDay;

  function bufferBefore(startDT, prev){
    // min gap after previous event
    let buf = minGap;
    if(prev){
      const sameLoc = (prev.location||'').toLowerCase() && (prev.location||'').toLowerCase() === (startDT.location||'').toLowerCase();
      buf = Math.max(buf, sameLoc ? travelSame : travelDiff);
    }
    return buf;
  }

  let prevFixed = null;
  for(let i=0;i<fixed.length;i++){
    const f = fixed[i];

    // shrink left by minGap after prev fixed + travel
    let gapStart = cursor;
    if(prevFixed){
      const sameLoc = (prevFixed.location||'').toLowerCase() && (prevFixed.location||'').toLowerCase() === (f.location||'').toLowerCase();
      const leftBuf = sameLoc ? travelSame : travelDiff;
      if(gapStart.isBefore(prevFixed.endDT.add(leftBuf,'minute'))) gapStart = prevFixed.endDT.add(leftBuf,'minute');
    } else {
      // first gap: ensure not before earliest
      if(gapStart.isBefore(startOfDay)) gapStart = startOfDay;
    }

    // shrink right by minGap before this fixed (and base travel to it)
    const rightBuf = f.location ? travelDiff : minGap;
    const gapEnd = f.startDT.subtract(rightBuf,'minute');

    if(gapEnd.isAfter(gapStart)) gaps.push({start: gapStart, end: gapEnd});

    cursor = f.endDT;
    prevFixed = f;
  }

  // Last trailing gap
  if(cursor.isBefore(endOfDay)){
    let tailStart = cursor.add(minGap,'minute');
    if(prevFixed && prevFixed.location) tailStart = prevFixed.endDT.add(travelDiff,'minute');
    const tailEnd = endOfDay;
    if(tailEnd.isAfter(tailStart)) gaps.push({start: tailStart, end: tailEnd});
  }

  // Place flex greedily into gaps
  const placed = [...fixed];
  const unplaced = [];

  function fits(gap, minutes){ return gap.end.diff(gap.start,'minute') >= minutes; }

  function placeOne(task){
    for(const g of gaps){
      if(!fits(g, task.duration_min)) continue;
      task.startDT = g.start;
      task.endDT   = g.start.add(task.duration_min,'minute');
      // advance gap start with minGap after this task
      g.start = task.endDT.add(minGap,'minute');
      placed.push(task);
      return true;
    }
    return false;
  }

  for(const t of flex){
    if(!placeOne(t)) unplaced.push(t);
  }

  // Normalize output
  let id=1;
  const items = placed.map(it=>{
    const start = it.startDT ? it.startDT.format('HH:mm') : (it.start||'');
    const end   = it.endDT ? it.endDT.format('HH:mm')   : (it.end||'');
    const dur   = it.duration_min || (start && end ? dayjs(baseDayIso+'T'+end).diff(dayjs(baseDayIso+'T'+start),'minute') : 0);
    return { id:id++, title: it.title||'Task', type: it.type|| (start&&end?'fixed':'flexible'),
      start, end, duration_min: dur, location: it.location||'', notes: it.notes||'' };
  }).sort((a,b)=>a.start.localeCompare(b.start));

  return { day: baseDayIso, items, unplaced };
}

/***************
 * Rendering   *
 ***************/
const startHr = 6, endHr = 24;

function renderPlanFor(day){
  store.planDay = day;
  const badge = $('#planDayBadge'); const label = $('#currentDayLabel');
  if(badge) badge.textContent = 'Day: ' + day;
  if(label) label.textContent = dayjs(day).format('ddd, MMM D, YYYY');
  renderPlan(getPlan(day));
}

function renderPlan(plan){
  const wrap = $('#planTable'); const unp = $('#unplaced');
  if(!wrap) return;

  if(!plan){ 
    wrap.innerHTML = '<div class="text-slate-500">No plan yet. Add items and click “Make / Update Plan”.</div>'; 
    if(unp) unp.innerHTML = '';
    renderTimeline(null); 
    return; 
  }

  const rows = plan.items.map(it=>`
    <div class="grid grid-cols-12 items-center gap-2 py-1 border-b border-slate-100">
      <input class="col-span-4 card !py-1" data-edit="title" data-id="${it.id}" value="${ESC(it.title)}" />
      <select class="col-span-2 card !py-1" data-edit="type" data-id="${it.id}">
        ${['fixed','flexible','meal','rest'].map(t=>`<option ${it.type===t?'selected':''}>${t}</option>`).join('')}
      </select>
      <input class="col-span-2 card !py-1" data-edit="start" data-id="${it.id}" value="${it.start}" placeholder="HH:mm"/>
      <input class="col-span-2 card !py-1" data-edit="end" data-id="${it.id}" value="${it.end}" placeholder="HH:mm"/>
      <input class="col-span-2 card !py-1" data-edit="location" data-id="${it.id}" value="${ESC(it.location||'')}" placeholder="location"/>
      <input class="col-span-12 card !py-1" data-edit="notes" data-id="${it.id}" value="${ESC(it.notes||'')}" placeholder="notes"/>
    </div>
  `).join('');

  wrap.innerHTML = `
    <div class="grid grid-cols-12 gap-2 text-xs text-slate-500 pb-1">
      <div class="col-span-4">Title</div>
      <div class="col-span-2">Type</div>
      <div class="col-span-2">Start</div>
      <div class="col-span-2">End</div>
      <div class="col-span-2">Location</div>
      <div class="col-span-12">Notes</div>
    </div>
    ${rows || '<div class="text-slate-500">No items.</div>'}
    <div class="pt-2"><button id="addRow" class="btn btn-ghost flex items-center gap-2"><i data-lucide="plus"></i>Add Item</button></div>
  `;

  wrap.querySelectorAll('[data-edit]').forEach(inp=>{
    inp.addEventListener('change', ()=>{
      const id = Number(inp.dataset.id), key = inp.dataset.edit, val = inp.value;
      const day = store.planDay; const p = getPlan(day) || emptyPlan(day);
      const it = p.items.find(x=>x.id===id); if(!it) return;
      it[key] = val; setPlan(day, p); renderPlan(p);
    });
  });
  $('#addRow')?.addEventListener('click', ()=>{
    const day = store.planDay; const p = getPlan(day) || emptyPlan(day);
    p.items.push({id:(p.items.at(-1)?.id||0)+1, title:'New Task', type:'flexible', start:'', end:'', duration_min:30, location:'', notes:''});
    setPlan(day, p); renderPlan(p);
  });

  // Unplaced
  if(unp){
    if(plan.unplaced?.length){
      unp.innerHTML = `
        <div class="font-semibold text-amber-700 mb-1">Unplaced items (no room within your bounds):</div>
        <ul class="list-disc pl-5">
          ${plan.unplaced.map(x=>`<li>${ESC(x.title)} (${x.duration_min||30}m)</li>`).join('')}
        </ul>
        <div class="hint mt-2">Tip: extend your latest end, reduce min gap, or remove a task to fit these.</div>
      `;
    } else {
      unp.innerHTML = '';
    }
  }

  lucide.createIcons();
  renderTimeline(plan);
}

function renderTimeline(plan){
  const tl = $('#timeline'); if(!tl) return;
  tl.innerHTML = '';
  const totalMin = (endHr - startHr)*60;
  for(let h=startHr; h<=endHr; h++){
    const y = ((h-startHr)/(endHr-startHr))*100;
    const line = document.createElement('div'); line.className='timeline-grid-line'; line.style.top = y+'%'; tl.appendChild(line);
    const lbl = document.createElement('div'); lbl.className='timeline-label'; lbl.style.top = y+'%'; lbl.textContent = (h<10?'0':'')+h+':00'; tl.appendChild(lbl);
  }
  const day = store.planDay;
  if(plan){
    for(const it of plan.items){
      if(!it.start || !it.end) continue;
      const s = dayjs(day+'T'+it.start), e = dayjs(day+'T'+it.end);
      const startMin = (s.hour()-startHr)*60 + s.minute();
      const durMin = Math.max(10, e.diff(s,'minute'));
      const topPct = (startMin/totalMin)*100;
      const heightPct = (durMin/totalMin)*100;
      const block = document.createElement('div');
      block.className='timeline-block';
      block.style.top = topPct+'%'; block.style.height=heightPct+'%';
      block.style.background = colorForType(it.type);
      block.innerHTML = `<div class="font-semibold text-[13px]">${ESC(it.title)}</div><div class="opacity-90">${it.start}–${it.end}${it.location? ' • '+ESC(it.location):''}</div>`;
      tl.appendChild(block);
    }
  }
  const today = dayjs().format('YYYY-MM-DD');
  if(day===today){
    const now = dayjs(); const nowMin = (now.hour()-startHr)*60 + now.minute();
    if(nowMin>=0 && nowMin<=totalMin){
      const nowLine = document.createElement('div'); nowLine.className='timeline-now';
      nowLine.style.top = ((nowMin/totalMin)*100)+'%'; tl.appendChild(nowLine);
    }
  }
}

function colorForType(t){
  if(t==='fixed') return '#2563eb';   // blue
  if(t==='meal')  return '#10b981';   // emerald
  if(t==='rest')  return '#8b5cf6';   // violet
  return '#f59e0b';                   // amber (flexible)
}

/***********************
 * Make / Update Plan  *
 ***********************/
$('#makePlan')?.addEventListener('click', async ()=>{
  const raw = $('#planInput').value.trim();
  if(!raw) return;
  const baseDay = detectBaseDay(raw);
  store.planDay = baseDay;

  // Load settings from UI
  const currentSettings = {
    earliest: $('#earliestStart').value || '08:00',
    latest:   $('#latestEnd').value   || '23:30',
    minGap:   Number($('#minGap').value || 10),
    travelDiff: Number($('#travelDiff').value || 20),
    travelSame: Number($('#travelSame').value || 10),
    homeBase: $('#homeBase').value || ''
  };
  store.settings = currentSettings;

  // AI-first, fallback to local
  let parsed;
  try{
    parsed = await aiDraft(raw, baseDay);
  }catch(err){
    console.warn('AI parse failed, falling back to local:', err);
    parsed = localDraft(raw);
  }

  // Merge with prior items (dedupe)
  const prior = getPlan(baseDay);
  const priorItems = prior ? prior.items : [];
  const mergedItems = dedupeMergeItems(priorItems, parsed.items || []);

  // Derive settings from items (wake/sleep context)
  const derived = deriveSettingsFromItems(mergedItems, currentSettings);
  const settings = derived.settings;
  const clar = derived.clarifications || [];
  renderClarifications(clar);

  // Allocate
  const finalPlan = allocatePlan(baseDay, { items: mergedItems }, settings);
  setPlan(baseDay, finalPlan);
  renderPlanFor(baseDay);
  scheduleLocalNotifications(finalPlan);
});

function renderClarifications(list){
  const el = $('#clarifications');
  if(!el) return;
  if(!list.length){ el.innerHTML = ''; return; }
  el.innerHTML = `<div class="mt-1 text-amber-700 font-medium">Questions to refine:</div>
    <ul class="list-disc pl-5">${list.map(x=>`<li>${ESC(x)}</li>`).join('')}</ul>`;
}

$('#resetPlan')?.addEventListener('click', ()=>{
  const day = store.planDay;
  const all = store.plans; delete all[day]; store.plans = all;
  renderPlanFor(day);
});

// Day nav
$('#prevDay')?.addEventListener('click', ()=>shiftDay(-1));
$('#nextDay')?.addEventListener('click', ()=>shiftDay(1));
$('#jumpToday')?.addEventListener('click', ()=>renderPlanFor(dayjs().format('YYYY-MM-DD')));
function shiftDay(d){ renderPlanFor(dayjs(store.planDay).add(d,'day').format('YYYY-MM-DD')); }

// .ics export
$('#exportICS')?.addEventListener('click', ()=>{
  const day = store.planDay; const p = getPlan(day);
  if(!p || !p.items?.length) return alert('No plan to export for '+day+'.');
  const cal = ics();
  for(const it of p.items){
    if(!it.start || !it.end) continue;
    const start = dayjs(day+'T'+it.start); const end = dayjs(day+'T'+it.end);
    cal.addEvent(it.title, it.notes||'', it.location || '',
      [start.year(), start.month()+1, start.date(), start.hour(), start.minute()],
      [end.year(), end.month()+1, end.date(), end.hour(), end.minute()],
      { reminders: [{method:'display', minutes:10}] }
    );
  }
  cal.download('plan_'+day);
});

function scheduleLocalNotifications(plan){
  if(Notification.permission!=='granted') return;
  const now = dayjs();
  for(const it of plan.items){
    if(!it.start) continue;
    const when = dayjs(plan.day+'T'+it.start).subtract(10,'minute');
    const ms = when.diff(now,'millisecond');
    if(ms>0 && ms<24*60*60*1000){
      setTimeout(()=>{ new Notification('Upcoming: '+it.title, { body: `${it.start}–${it.end} ${it.location? '• '+it.location : ''}` }); }, ms);
    }
  }
}

/***************
 * Utils       *
 ***************/
function detectBaseDay(text){
  if(/tomorrow/i.test(text)) return dayjs().add(1,'day').format('YYYY-MM-DD');
  if(/today/i.test(text)) return dayjs().format('YYYY-MM-DD');
  const p = chrono.parse(text, new Date());
  if(p?.length){
    const d = p[0].start?.date();
    if(d) return dayjs(d).format('YYYY-MM-DD');
  }
  return store.planDay || dayjs().format('YYYY-MM-DD');
}

/************
 * Flows    *
 ************/
async function llmFlow(text){
  const sys = `Return STRICT JSON: {"title":string,"steps":[{"id":1,"title":string,"detail":string,"status":"todo"|"doing"|"done"}...],"notes":string}`;
  const out = await askOpenAI([{role:'system', content:sys},{role:'user', content:text}], {temperature:0.2, json:true});
  return JSON.parse(out);
}
function renderFlow(flow){
  const wrap = $('#flowSteps'); if(!wrap) return;
  if(!flow){ wrap.innerHTML='<div class="text-slate-500">No flow yet.</div>'; return; }
  const steps = flow.steps?.map(s=>`
    <div class="p-3 rounded-xl border bg-white flex gap-3 items-start border-slate-100">
      <input type="checkbox" data-step="${s.id}" ${s.status==='done'?'checked':''} class="mt-1"/>
      <div class="flex-1">
        <div class="font-semibold">${ESC(s.title)}</div>
        <div class="text-sm text-slate-600 whitespace-pre-wrap">${ESC(s.detail)}</div>
        <div class="mt-1">
          <select data-stat="${s.id}" class="card !py-1">
            ${['todo','doing','done'].map(st=>`<option ${s.status===st?'selected':''}>${st}</option>`).join('')}
          </select>
        </div>
      </div>
    </div>`).join('');
  wrap.innerHTML = `
    <div class="mb-2">
      <textarea id="flowNotes" class="w-full card !py-2 mt-2" rows="2" placeholder="Notes...">${ESC(flow.notes||'')}</textarea>
      <div class="mt-2"><button id="saveNotes" class="btn btn-ghost flex items-center gap-2"><i data-lucide="save"></i>Save Notes</button></div>
    </div>
    <div class="space-y-2">${steps||''}</div>`;
  wrap.querySelectorAll('[data-step]').forEach(cb=>{
    cb.addEventListener('change', ()=>{ const id=+cb.dataset.step; const f=JSON.parse(localStorage.getItem('FLOW_V1')||'null'); if(!f) return; const st=f.steps.find(x=>x.id===id); if(!st)return; st.status=cb.checked?'done':'todo'; localStorage.setItem('FLOW_V1', JSON.stringify(f)); renderFlow(f); });
  });
  wrap.querySelectorAll('[data-stat]').forEach(sel=>{
    sel.addEventListener('change', ()=>{ const id=+sel.dataset.stat; const f=JSON.parse(localStorage.getItem('FLOW_V1')||'null'); if(!f) return; const st=f.steps.find(x=>x.id===id); if(!st)return; st.status=sel.value; localStorage.setItem('FLOW_V1', JSON.stringify(f)); });
  });
  $('#saveNotes')?.addEventListener('click', ()=>{ const f=JSON.parse(localStorage.getItem('FLOW_V1')||'null'); if(!f) return; f.notes=$('#flowNotes').value; localStorage.setItem('FLOW_V1', JSON.stringify(f)); alert('Saved.'); });
  lucide.createIcons();
}
$('#makeFlow')?.addEventListener('click', async ()=>{
  const text = $('#flowInput').value.trim(); if(!text) return;
  try{ const f = await llmFlow(text); localStorage.setItem('FLOW_V1', JSON.stringify(f)); renderFlow(f); }
  catch(e){ alert('Flows need a valid OpenAI key. Error: '+e.message); }
});
$('#resetFlow')?.addEventListener('click', ()=>{ localStorage.removeItem('FLOW_V1'); renderFlow(null); });

/***************
 * Boot        *
 ***************/
(function init(){
  // Restore settings into the UI
  const s = store.settings;
  $('#earliestStart').value = s.earliest || '08:00';
  $('#latestEnd').value = s.latest || '23:30';
  $('#minGap').value = s.minGap ?? 10;
  $('#travelSame').value = s.travelSame ?? 10;
  $('#travelDiff').value = s.travelDiff ?? 20;
  $('#homeBase').value = s.homeBase || '';

  renderPlanFor(store.planDay);
  const savedFlow = JSON.parse(localStorage.getItem('FLOW_V1')||'null'); renderFlow(savedFlow);
})();
