/********************
 * Bootstrapping    *
 ********************/
window.addEventListener('DOMContentLoaded', ()=>{ lucide.createIcons(); });

const $ = (s)=>document.querySelector(s);
const $$ = (s)=>document.querySelectorAll(s);

// Hardened escape: ALWAYS coerce to string first
const ESC = (s)=> String(s ?? '').replace(/[&<>"']/g, m=>({ '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;', "'":'&#39;' }[m]));

// Timeline vertical bounds (once!)
const START_HR = 6;  // 06:00
const END_HR   = 24; // 24:00

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

  // Per-day plans: { "YYYY-MM-DD": { day, items:[...], unplaced:[...]} }
  get plans(){ return JSON.parse(localStorage.getItem('PLANS_BY_DAY') || '{}'); },
  set plans(v){ localStorage.setItem('PLANS_BY_DAY', JSON.stringify(v)); },

  get planDay(){ return localStorage.getItem('PLAN_DAY') || dayjs().format('YYYY-MM-DD'); },
  set planDay(v){ localStorage.setItem('PLAN_DAY', v); },

  get settings(){ return JSON.parse(localStorage.getItem('PLAN_SETTINGS') || '{"earliest":"08:00","latest":"23:30","minGap":10,"travelSame":10,"travelDiff":20,"homeBase":""}'); },
  set settings(v){ localStorage.setItem('PLAN_SETTINGS', JSON.stringify(v)); },

  // Projects: { id: {id,title,type,budget,start,end,tags,brief,history:[...], tasks:[...], itinerary:[...], insights:[...], widgets:[...] } }
  get projects(){ return JSON.parse(localStorage.getItem('PROJECTS_V1') || '{}'); },
  set projects(v){ localStorage.setItem('PROJECTS_V1', JSON.stringify(v)); },

  get currentProjectId(){ return localStorage.getItem('CURRENT_PROJECT_ID') || ''; },
  set currentProjectId(v){ localStorage.setItem('CURRENT_PROJECT_ID', v); },
};

function getPlan(day){ return store.plans[day] || null; }
function setPlan(day, plan){ const all = store.plans; all[day] = plan; store.plans = all; }

function getProject(id){ return store.projects[id] || null; }
function setProject(p){ const all = store.projects; all[p.id] = p; store.projects = all; }

/*****************
 * Tabs & header *
 *****************/
(function initTabs(){
  const tabs = $$('.tab');
  const sections = ['projects','plan','chat'];
  tabs.forEach(btn=>{
    btn.addEventListener('click', ()=>{
      sections.forEach(id=>$('#'+id).classList.add('hidden'));
      $('#'+btn.dataset.tab).classList.remove('hidden');
      tabs.forEach(b=>b.classList.remove('bg-blue-600','text-white'));
      btn.classList.add('bg-blue-600','text-white');
      lucide.createIcons();
      if(btn.dataset.tab==='projects') renderProjectList();
      if(btn.dataset.tab==='plan') renderPlanFor(store.planDay);
    });
  });
  document.querySelector('[data-tab="projects"]').click();

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
function emptyPlan(dayIso){ return { day: dayIso, items: [], unplaced: [] }; }

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

/* Local fallback (if OpenAI fails) */
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

    if(/\bwake\s*up\b/i.test(line) && !durMin && !parsed.length){
      items.push({ title:'Wake up', type:'marker', start:'', end:'', duration_min:0, location:'', notes:'' });
      continue;
    }
    if(/\bsleep\b/i.test(line) && /by/i.test(line) && !parsed.length){
      items.push({ title:'Sleep', type:'rest', start:'', end:'23:00', duration_min:0, location:'', notes:'' });
      continue;
    }

    const d = durMin || defaultDur(line);
    const isMeal = /\b(breakfast|lunch|dinner)\b/i.test(line);
    items.push({ title:titleWithCompanion(), type: isMeal ? 'meal' : 'flexible', location, start:'', end:'', duration_min:d, notes:'' });
  }
  return { items };
}

/* AI-first parsing for daily plan */
async function aiDraft(text, baseDayIso){
  const sys = `
You are a scheduling parser. Return STRICT JSON:
{"day":"YYYY-MM-DD","items":[{"title":string,"type":"fixed"|"flexible"|"meal"|"rest"|"marker","start":"HH:mm","end":"HH:mm","duration_min":0,"location":"","notes":""}]}
Rules:
- Assume date ${baseDayIso}.
- Parse "9-9:50am" etc. Keep companions in title; "at X" -> location.
- Meals w/out times default durations: breakfast 30m, lunch 45m, dinner 45m.
- "Sleep by 11" -> rest with end "23:00" if time not explicit; "Wake up" can be "marker" without time.
- Output ONLY JSON.`;
  const out = await askOpenAI([{role:'system',content:sys},{role:'user',content:text}], {temperature:0.1, json:true});
  return JSON.parse(out);
}

/* Deduplication for daily plan */
function canonicalTitle(s){
  return String(s||'').toLowerCase().replace(/\s+/g,' ').replace(/[.,;:!?]+/g,'').trim();
}
function timesKey(s,e){ return (s&&e) ? `${s}-${e}` : ''; }
function dedupeMergeItems(existingItems, newItems){
  const merged = [...existingItems];
  const seen = new Map();
  for(let i=0;i<merged.length;i++){
    seen.set(canonicalTitle(merged[i].title)+'|'+timesKey(merged[i].start, merged[i].end), i);
  }
  for(const it of newItems){
    const key = canonicalTitle(it.title)+'|'+timesKey(it.start, it.end);
    if(seen.has(key)){
      const idx = seen.get(key);
      if((it.notes||'').length > (merged[idx].notes||'').length) merged[idx].notes = it.notes;
      if((it.location||'').length > (merged[idx].location||'').length) merged[idx].location = it.location;
      continue;
    }
    if(!it.start && !it.end){
      const dupIdx = merged.findIndex(x => !x.start && !x.end && canonicalTitle(x.title)===canonicalTitle(it.title));
      if(dupIdx>=0){
        if((it.duration_min||0) > (merged[dupIdx].duration_min||0)) merged[dupIdx].duration_min = it.duration_min;
        if((it.notes||'').length > (merged[dupIdx].notes||'').length) merged[dupIdx].notes = it.notes;
        continue;
      }
    }
    seen.set(key, merged.length); merged.push(it);
  }
  return merged;
}

/* Derive settings (wake/sleep) */
function deriveSettingsFromItems(items, baseSettings){
  const s = {...baseSettings};
  const wake = items.find(x=>/wake\s*up/i.test(String(x.title)) && x.start);
  if(wake) s.earliest = wake.start;
  const sleep = items.find(x=>x.type==='rest' && x.end && !x.start);
  if(sleep) s.latest = sleep.end;

  const clarifications = [];
  if(!wake && items.some(x=>/wake\s*up/i.test(String(x.title)))) {
    clarifications.push('What time do you want to wake up? (Using '+s.earliest+' for now)');
  }
  return {settings: s, clarifications};
}

/* Allocation with buffers */
function allocatePlan(baseDayIso, draft, settings){
  const earliest = settings.earliest || '08:00';
  const latest   = settings.latest   || '23:30';
  const minGap   = Number(settings.minGap || 10);
  const travelSame = Number(settings.travelSame || 10);
  const travelDiff = Number(settings.travelDiff || 20);

  const toTime = (hhmm)=>dayjs(baseDayIso + 'T' + String(hhmm||'').padStart(5,'0'));
  const startOfDay = toTime(earliest);
  const endOfDay   = toTime(latest);

  const fixed=[], flex=[];
  for(const raw of (draft.items||[])){
    const it = {...raw};
    if(it.start && it.end){
      fixed.push({...it, startDT: toTime(it.start), endDT: toTime(it.end)});
      continue;
    }
    let d = Number(it.duration_min)||0;
    if(it.type==='meal' && !d){
      const t = String(it.title||'').toLowerCase();
      d = t.includes('breakfast') ? 30 : (t.includes('lunch') || t.includes('dinner')) ? 45 : 30;
    }
    flex.push({...it, duration_min: d || 30});
  }

  fixed.sort((a,b)=>a.startDT - b.startDT);

  const gaps = [];
  let cursor = startOfDay;
  let prevFixed = null;

  for(const f of fixed){
    let gapStart = cursor;
    if(prevFixed){
      const sameLoc = (String(prevFixed.location||'').toLowerCase()) && (String(prevFixed.location||'').toLowerCase() === String(f.location||'').toLowerCase());
      const leftBuf = sameLoc ? travelSame : travelDiff;
      if(gapStart.isBefore(prevFixed.endDT.add(leftBuf,'minute'))) gapStart = prevFixed.endDT.add(leftBuf,'minute');
    } else {
      if(gapStart.isBefore(startOfDay)) gapStart = startOfDay;
    }
    const rightBuf = f.location ? travelDiff : minGap;
    const gapEnd = f.startDT.subtract(rightBuf,'minute');
    if(gapEnd.isAfter(gapStart)) gaps.push({start: gapStart, end: gapEnd});

    cursor = f.endDT;
    prevFixed = f;
  }

  if(cursor.isBefore(endOfDay)){
    let tailStart = cursor.add(minGap,'minute');
    if(prevFixed && prevFixed.location) tailStart = prevFixed.endDT.add(travelDiff,'minute');
    const tailEnd = endOfDay;
    if(tailEnd.isAfter(tailStart)) gaps.push({start: tailStart, end: tailEnd});
  }

  const placed = [...fixed];
  const unplaced = [];

  function fits(gap, minutes){ return gap.end.diff(gap.start,'minute') >= minutes; }
  function placeOne(task){
    for(const g of gaps){
      if(!fits(g, task.duration_min)) continue;
      task.startDT = g.start;
      task.endDT   = g.start.add(task.duration_min,'minute');
      g.start = task.endDT.add(minGap,'minute');
      placed.push(task);
      return true;
    }
    return false;
  }
  for(const t of flex){ if(!placeOne(t)) unplaced.push(t); }

  let id=1;
  const items = placed.map(it=>{
    const start = it.startDT ? it.startDT.format('HH:mm') : (it.start||'');
    const end   = it.endDT ? it.endDT.format('HH:mm')   : (it.end||'');
    const dur   = it.duration_min || (start && end ? dayjs(baseDayIso+'T'+end).diff(dayjs(baseDayIso+'T'+start),'minute') : 0);
    return { id:id++, title: it.title||'Task', type: it.type|| (start&&end?'fixed':'flexible'),
      start, end, duration_min: dur, location: it.location||'', notes: it.notes||'' };
  }).sort((a,b)=>String(a.start||'').localeCompare(String(b.start||'')));

  return { day: baseDayIso, items, unplaced };
}

/***************
 * Rendering (Plan)
 ***************/
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
      <input class="col-span-2 card !py-1" data-edit="start" data-id="${it.id}" value="${ESC(it.start)}" placeholder="HH:mm"/>
      <input class="col-span-2 card !py-1" data-edit="end" data-id="${it.id}" value="${ESC(it.end)}" placeholder="HH:mm"/>
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

  if(unp){
    if(plan.unplaced?.length){
      unp.innerHTML = `
        <div class="font-semibold text-amber-700 mb-1">Unplaced items (no room within your bounds):</div>
        <ul class="list-disc pl-5">
          ${plan.unplaced.map(x=>`<li>${ESC(x.title)} (${ESC(x.duration_min||30)}m)</li>`).join('')}
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
  const totalMin = (END_HR - START_HR)*60;
  for(let h=START_HR; h<=END_HR; h++){
    const y = ((h-START_HR)/(END_HR-START_HR))*100;
    const line = document.createElement('div'); line.className='timeline-grid-line'; line.style.top = y+'%'; tl.appendChild(line);
    const lbl = document.createElement('div'); lbl.className='timeline-label'; lbl.style.top = y+'%'; lbl.textContent = (h<10?'0':'')+h+':00'; tl.appendChild(lbl);
  }
  const day = store.planDay;
  if(plan){
    for(const it of plan.items){
      if(!it.start || !it.end) continue;
      const s = dayjs(day+'T'+it.start), e = dayjs(day+'T'+it.end);
      const startMin = (s.hour()-START_HR)*60 + s.minute();
      const durMin = Math.max(10, e.diff(s,'minute'));
      const topPct = (startMin/totalMin)*100;
      const heightPct = (durMin/totalMin)*100;
      const block = document.createElement('div');
      block.className='timeline-block';
      block.style.top = topPct+'%'; block.style.height=heightPct+'%';
      block.style.background = colorForType(it.type);
      block.innerHTML = `<div class="font-semibold text-[13px]">${ESC(it.title)}</div><div class="opacity-90">${ESC(it.start)}–${ESC(it.end)}${it.location? ' • '+ESC(it.location):''}</div>`;
      tl.appendChild(block);
    }
  }
  const today = dayjs().format('YYYY-MM-DD');
  if(day===today){
    const now = dayjs(); const nowMin = (now.hour()-START_HR)*60 + now.minute();
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

  const currentSettings = {
    earliest: $('#earliestStart').value || '08:00',
    latest:   $('#latestEnd').value   || '23:30',
    minGap:   Number($('#minGap').value || 10),
    travelDiff: Number($('#travelDiff').value || 20),
    travelSame: Number($('#travelSame').value || 10),
    homeBase: $('#homeBase').value || ''
  };
  store.settings = currentSettings;

  let parsed;
  try{
    parsed = await aiDraft(raw, baseDay);
  }catch(err){
    console.warn('AI parse failed, falling back to local:', err);
    parsed = localDraft(raw);
  }

  const prior = getPlan(baseDay);
  const priorItems = prior ? prior.items : [];
  const mergedItems = dedupeMergeItems(priorItems, parsed.items || []);

  const derived = deriveSettingsFromItems(mergedItems, currentSettings);
  const settings = derived.settings;
  const clar = derived.clarifications || [];
  renderClarifications(clar);

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

/***********************
 * PROJECTS (new)      *
 ***********************/
$('#newProject')?.addEventListener('click', ()=>{
  const id = 'p_' + Math.random().toString(36).slice(2,9);
  const p = { id, title:'Untitled Project', type:'', budget:'', start:'', end:'', tags:'', brief:'', history:[], tasks:[], itinerary:[]
            , insights:[], widgets:[], followups:[] };
  setProject(p); store.currentProjectId = id;
  renderProjectList(); loadProjectIntoUI(p);
});

$('#clearProject')?.addEventListener('click', ()=>{
  const id = store.currentProjectId; if(!id) return;
  const p = getProject(id); if(!p) return;
  p.tasks=[]; p.itinerary=[]; p.insights=[]; p.widgets=[]; p.followups=[]; p.history=[]; p.brief='';
  setProject(p); loadProjectIntoUI(p);
});

function renderProjectList(){
  const wrap = $('#projectList'); if(!wrap) return;
  const all = Object.values(store.projects);
  if(!all.length){ wrap.innerHTML = '<div class="text-slate-500 p-2">No projects yet. Click <b>New</b>.</div>'; return; }
  wrap.innerHTML = all.map(p=>`
    <div class="p-2 rounded-lg border ${p.id===store.currentProjectId?'bg-indigo-50 border-indigo-200':'bg-white border-slate-200'} hover:bg-slate-50 cursor-pointer flex items-center justify-between" data-pid="${p.id}">
      <div>
        <div class="font-semibold">${ESC(p.title||'Untitled')}</div>
        <div class="text-xs text-slate-500">${ESC(p.type||'')}${p.start? ' • '+ESC(p.start):''}${p.end? ' → '+ESC(p.end):''}</div>
      </div>
      <button class="btn btn-ghost text-red-600" data-del="${p.id}"><i data-lucide="trash"></i></button>
    </div>
  `).join('');
  wrap.querySelectorAll('[data-pid]').forEach(el=>{
    el.addEventListener('click', (ev)=>{
      const pid = el.dataset.pid;
      if((ev.target.closest('button')?.dataset.del)) return;
      store.currentProjectId = pid;
      renderProjectList(); loadProjectIntoUI(getProject(pid));
    });
  });
  wrap.querySelectorAll('[data-del]').forEach(btn=>{
    btn.addEventListener('click', ()=>{
      const id = btn.dataset.del;
      const all = store.projects; delete all[id]; store.projects = all;
      if(store.currentProjectId===id) store.currentProjectId='';
      renderProjectList(); clearProjectUI();
    });
  });
  lucide.createIcons();
}

function clearProjectUI(){
  $('#projectHeader').innerHTML = `<span class="pill">No project selected</span>`;
  $('#projTitle').value=''; $('#projType').value=''; $('#projBudget').value='';
  $('#projStart').value=''; $('#projEnd').value=''; $('#projTags').value='';
  $('#projBrief').value='';
  $('#projTasks').innerHTML=''; $('#projItin').innerHTML=''; $('#projInsights').innerHTML=''; $('#projWidgets').innerHTML=''; $('#projFollowups').innerHTML='';
}

function loadProjectIntoUI(p){
  if(!p){ clearProjectUI(); return; }
  // Sanitize before rendering (coerce types to strings where needed)
  sanitizeProjectInPlace(p);

  $('#projectHeader').innerHTML = `
    <span class="pill">${ESC(p.title || 'Untitled Project')}</span>
    ${p.type? `<span class="pill">${ESC(p.type)}</span>`:''}
    ${p.budget? `<span class="pill">Budget: ${ESC(p.budget)}</span>`:''}
    ${p.start? `<span class="pill">Start: ${ESC(p.start)}</span>`:''}
    ${p.end? `<span class="pill">End: ${ESC(p.end)}</span>`:''}
  `;
  $('#projTitle').value = p.title||'';
  $('#projType').value  = p.type||'';
  $('#projBudget').value= p.budget||'';
  $('#projStart').value = p.start||'';
  $('#projEnd').value   = p.end||'';
  $('#projTags').value  = p.tags||'';
  $('#projBrief').value = p.brief||'';

  renderTasks(p.tasks||[]);
  renderItinerary(p.itinerary||[]);
  renderInsights(p.insights||[]);
  renderWidgets(p.widgets||[]);
  renderFollowups(p.followups||[]);
}

['projTitle','projType','projBudget','projStart','projEnd','projTags','projBrief'].forEach(id=>{
  $('#'+id)?.addEventListener('change', ()=>{
    const pid = store.currentProjectId; if(!pid) return;
    const p = getProject(pid); if(!p) return;
    p.title   = $('#projTitle').value.trim();
    p.type    = $('#projType').value.trim();
    p.budget  = $('#projBudget').value.trim();
    p.start   = $('#projStart').value.trim();
    p.end     = $('#projEnd').value.trim();
    p.tags    = $('#projTags').value.trim();
    p.brief   = $('#projBrief').value.trim();
    setProject(p); loadProjectIntoUI(p); renderProjectList();
  });
});

/* Project AI generation */
$('#genProject')?.addEventListener('click', async ()=>{
  const pid = store.currentProjectId || (()=>{
    const id = 'p_' + Math.random().toString(36).slice(2,9);
    const p = { id, title:'Untitled Project', type:'', budget:'', start:'', end:'', tags:'', brief:'', history:[], tasks:[], itinerary:[], insights:[], widgets:[], followups:[] };
    setProject(p); store.currentProjectId=id; renderProjectList(); return id;
  })();

  const p = getProject(pid);
  const brief = {
    title: $('#projTitle').value || p.title,
    type: $('#projType').value || p.type,
    budget: $('#projBudget').value || p.budget,
    start: $('#projStart').value || p.start,
    end: $('#projEnd').value || p.end,
    tags: ($('#projTags').value || p.tags || '').split(',').map(s=>s.trim()).filter(Boolean),
    notes: $('#projBrief').value || p.brief
  };

  try{
    const res = await aiProjectPlanner(brief, p);
    // Normalize incoming before merging
    const projectOut = sanitizeProjectOutput(res?.project || {});
    // Merge/replace
    p.title   = projectOut.title || p.title;
    p.type    = projectOut.type  || p.type;
    p.budget  = projectOut.budget|| p.budget;
    p.start   = projectOut.start || p.start;
    p.end     = projectOut.end   || p.end;
    p.widgets = projectOut.widgets || [];
    p.insights= projectOut.insights || [];
    p.followups = projectOut.followups || [];
    p.tasks   = mergeTasks(p.tasks, projectOut.tasks || []);
    p.itinerary = mergeItinerary(p.itinerary, projectOut.itinerary || []);
    p.brief = brief.notes;
    p.history.push({ ts: Date.now(), brief });

    setProject(p); loadProjectIntoUI(p); renderProjectList();
  }catch(e){
    alert('Project AI error: ' + e.message);
  }
});

/* Project → Schedule sync */
$('#syncToSchedule')?.addEventListener('click', ()=>{
  const pid = store.currentProjectId; if(!pid) return alert('Select a project first.');
  const p = getProject(pid); if(!p || !(p.itinerary||[]).length) return alert('No itinerary to sync.');
  const defaults = store.settings || { earliest:'08:00', latest:'23:30', minGap:10, travelDiff:20, travelSame:10 };
  const byDate = {};
  for(const it of p.itinerary){
    const day = it.date;
    if(!day) continue;
    byDate[day] = byDate[day] || [];
    const item = {
      title: it.title,
      type: (it.start && it.end) ? 'fixed' : 'flexible',
      start: it.start || '',
      end: it.end || '',
      duration_min: it.duration_min || (it.start && it.end ? dayjs(day+'T'+it.end).diff(dayjs(day+'T'+it.start),'minute'):30),
      location: it.location || '',
      notes: it.notes || `[${p.title}]`
    };
    byDate[day].push(item);
  }

  for(const day of Object.keys(byDate)){
    const prior = getPlan(day) || emptyPlan(day);
    const mergedItems = dedupeMergeItems(prior.items, byDate[day]);
    const finalPlan = allocatePlan(day, { items: mergedItems }, defaults);
    setPlan(day, finalPlan);
  }
  alert('Itinerary synced to your Schedule. Open the Plan tab to review.');
});

/* ---- Project rendering helpers ---- */
function renderTasks(tasks){
  const wrap = $('#projTasks'); if(!wrap) return;
  if(!tasks.length){ wrap.innerHTML = '<div class="text-slate-500">No tasks yet.</div>'; return; }
  wrap.innerHTML = tasks.map(t=>`
    <div class="p-2 rounded-lg border bg-white border-slate-200 flex items-start gap-2">
      <input type="checkbox" ${t.status==='done'?'checked':''} data-task="${ESC(t.id)}" class="mt-1">
      <div class="flex-1">
        <div class="font-semibold">${ESC(t.title)}</div>
        <div class="text-sm text-slate-600 whitespace-pre-wrap">${ESC(t.detail||'')}</div>
        <div class="text-xs text-slate-500 mt-1">Owner: ${ESC(t.owner||'me')} • ${ESC(t.category||'general')} • ${ESC(t.eta_min||'')}${t.eta_min?'m':''}</div>
      </div>
    </div>
  `).join('');
  wrap.querySelectorAll('[data-task]').forEach(cb=>{
    cb.addEventListener('change', ()=>{
      const pid = store.currentProjectId; if(!pid) return;
      const p = getProject(pid); if(!p) return;
      const task = p.tasks.find(x=>String(x.id)===cb.dataset.task); if(!task) return;
      task.status = cb.checked ? 'done' : 'todo';
      setProject(p); renderTasks(p.tasks);
    });
  });
}

function renderItinerary(it){
  const wrap = $('#projItin'); if(!wrap) return;
  if(!it.length){ wrap.innerHTML = '<div class="text-slate-500">No itinerary yet.</div>'; return; }
  const groups = {};
  for(const item of it){ groups[item.date] = groups[item.date] || []; groups[item.date].push(item); }
  wrap.innerHTML = Object.keys(groups).sort().map(date=>{
    const rows = groups[date].map(x=>`
      <div class="grid grid-cols-12 items-center gap-2 py-1 border-b border-slate-100">
        <div class="col-span-3">${ESC(x.start||'')}${x.end? ' – '+ESC(x.end):''}</div>
        <div class="col-span-5 font-semibold">${ESC(x.title)}</div>
        <div class="col-span-4 text-slate-600">${ESC(x.location||'')}${x.notes? ' • '+ESC(x.notes):''}</div>
      </div>
    `).join('');
    return `<div class="mb-2">
      <div class="font-semibold">${dayjs(date).format('ddd, MMM D, YYYY')} (${ESC(date)})</div>
      <div class="mt-1">${rows}</div>
    </div>`;
  }).join('');
}

function renderInsights(ins){
  const wrap = $('#projInsights'); if(!wrap) return;
  if(!ins.length){ wrap.innerHTML = '<div class="text-slate-500">No insights yet. Generate to see suggestions.</div>'; return; }
  wrap.innerHTML = '<ul class="list-disc pl-5 space-y-1">'+ins.map(i=>`<li>${ESC(i)}</li>`).join('')+'</ul>';
}

function renderWidgets(w){
  const wrap = $('#projWidgets'); if(!wrap) return;
  if(!w.length){ wrap.innerHTML = '<div class="text-slate-500">No widgets yet.</div>'; return; }
  wrap.innerHTML = w.map(x=>`
    <div class="p-3 rounded-xl border bg-white border-slate-200">
      <div class="text-xs text-slate-500">${ESC(x.name)}</div>
      <div class="text-xl font-extrabold">${ESC(x.value)}<span class="text-sm font-semibold ml-1">${ESC(x.unit||'')}</span></div>
    </div>
  `).join('');
}

function renderFollowups(qs){
  const wrap = $('#projFollowups'); if(!wrap) return;
  if(!qs.length){ wrap.innerHTML = '<div class="text-slate-500">No questions. The AI will add clarifying questions here.</div>'; return; }
  wrap.innerHTML = qs.map((q,i)=>`
    <div class="flex items-center justify-between p-2 rounded-lg border bg-white border-slate-200 mt-1">
      <div>${ESC(q)}</div>
      <button class="btn btn-ghost kbd" data-answer="${i}">Answer</button>
    </div>
  `).join('');
  wrap.querySelectorAll('[data-answer]').forEach(btn=>{
    btn.addEventListener('click', ()=>{
      const idx = +btn.dataset.answer;
      const ans = prompt('Your answer:'); if(!ans) return;
      const pid = store.currentProjectId; const p = getProject(pid); if(!p) return;
      p.history.push({ts:Date.now(), answer:{q:p.followups[idx], a:ans}});
      p.brief = (p.brief||'') + `\nAnswer: ${p.followups[idx]} -> ${ans}`;
      setProject(p); loadProjectIntoUI(p);
      alert('Answer saved. Click "Generate / Update Plan" to refine the project.');
    });
  });
}

/* Merge helpers */
function mergeTasks(existing, incoming){
  const byKey = new Map();
  const key = (t)=> String(t.title||'').toLowerCase().trim();
  existing.forEach(t=>byKey.set(key(t), t));
  for(const t of incoming){
    const k = key(t);
    if(!byKey.has(k)){ byKey.set(k, t); continue; }
    const old = byKey.get(k);
    old.detail = (String(t.detail||'').length > String(old.detail||'').length) ? t.detail : old.detail;
    old.category = old.category || t.category;
    old.eta_min = old.eta_min || t.eta_min;
  }
  return Array.from(byKey.values());
}
function mergeItinerary(existing, incoming){
  const out = [...existing];
  const has = (a,b)=> a.date===b.date && (a.start||'')===(b.start||'') && (a.end||'')===(b.end||'') && String(a.title||'').toLowerCase().trim()===String(b.title||'').toLowerCase().trim();
  for(const it of incoming){
    if(out.some(x=>has(x,it))) continue;
    out.push(it);
  }
  out.sort((a,b)=> (a.date||'').localeCompare(b.date||'') || (a.start||'').localeCompare(b.start||''));
  return out;
}

/* --------- AI: Project Planner & Insight Engine ---------- */
async function aiProjectPlanner(brief, currentProject){
  const sys = `
You are a pragmatic project planner, itinerary builder, and insight engine.
Return STRICT JSON ONLY in this schema:

{
  "project": {
    "title": string,
    "type": string,
    "budget": string,
    "start": "YYYY-MM-DD",
    "end": "YYYY-MM-DD",
    "summary": string,
    "widgets": [{"name":string,"value":number|string,"unit":string}],
    "insights": [string],
    "followups": [string],
    "tasks": [
      {"id": string|number, "title": string, "detail": string, "status":"todo"|"doing"|"done", "category": string, "owner": string, "eta_min": number}
    ],
    "itinerary": [
      {"date": "YYYY-MM-DD", "title": string, "start": "HH:mm", "end": "HH:mm", "duration_min": number, "location": string, "notes": string}
    ]
  }
}

Rules:
- Be decisive: fill gaps with reasonable defaults; ask only a few high-impact follow-up questions.
- Use budget/dates/type to tailor tasks and insights.
- If user provided full itinerary, keep assistance light; still add 2–4 thoughtful suggestions if time exists.
- Include widgets for the crucial KPIs (e.g., "Total days", "Budget", "Avg daily budget", "Flights cost", "Buffer hours").
- Keep "itinerary" chronological; times may be approximate if not provided.
- Keep "tasks" actionable (verbs), 5–12 items typical; categorize (flights, lodging, activities, docs, packing, misc).
- Never include non-JSON commentary.`;

  const user = {
    role:'user',
    content: `Brief:
title=${brief.title}
type=${brief.type}
budget=${brief.budget}
start=${brief.start}
end=${brief.end}
tags=${brief.tags.join(', ')}
notes:
${brief.notes}

Current project (for context):
${JSON.stringify({
  title: currentProject.title,
  type: currentProject.type,
  budget: currentProject.budget,
  start: currentProject.start,
  end: currentProject.end,
  tasks: (currentProject.tasks||[]).slice(0,20),
  itinerary: (currentProject.itinerary||[]).slice(0,40)
})}`
  };

  const out = await askOpenAI(
    [
      {role:'system', content: sys},
      user
    ],
    { temperature: 0.2, json: true }
  );
  return JSON.parse(out);
}

/************
 * Sanitizers
 ************/
function sanitizeProjectOutput(pr){
  const safeArr = (x)=> Array.isArray(x)? x : [];
  const asStr = (x)=> String(x ?? '');
  const asNumOrStr = (x)=> (typeof x==='number' || typeof x==='string') ? x : '';
  const tasks = safeArr(pr.tasks).map((t,i)=>({
    id: t?.id ?? i+1,
    title: asStr(t?.title),
    detail: asStr(t?.detail),
    status: ['todo','doing','done'].includes(String(t?.status)) ? t.status : 'todo',
    category: asStr(t?.category),
    owner: asStr(t?.owner || 'me'),
    eta_min: (typeof t?.eta_min==='number') ? t.eta_min : (parseInt(t?.eta_min,10) || '')
  }));
  const itinerary = safeArr(pr.itinerary).map(it=>({
    date: asStr(it?.date),
    title: asStr(it?.title),
    start: asStr(it?.start||''),
    end: asStr(it?.end||''),
    duration_min: (typeof it?.duration_min==='number') ? it.duration_min :
                  (it?.start && it?.end ? undefined : 30),
    location: asStr(it?.location||''),
    notes: asStr(it?.notes||'')
  }));
  const widgets = safeArr(pr.widgets).map(w=>({
    name: asStr(w?.name),
    value: asNumOrStr(w?.value),
    unit: asStr(w?.unit||'')
  }));
  const insights = safeArr(pr.insights).map(asStr);
  const followups = safeArr(pr.followups).map(asStr);
  return {
    title: asStr(pr.title),
    type: asStr(pr.type),
    budget: asStr(pr.budget),
    start: asStr(pr.start),
    end: asStr(pr.end),
    widgets, insights, followups, tasks, itinerary
  };
}
function sanitizeProjectInPlace(p){
  // Coerce any arrays that might contain non-strings to strings where needed.
  p.title = String(p.title||''); p.type=String(p.type||''); p.budget=String(p.budget||'');
  p.start=String(p.start||''); p.end=String(p.end||''); p.tags=String(p.tags||'');
  p.insights = (Array.isArray(p.insights)? p.insights : []).map(x=>String(x??''));
  p.followups= (Array.isArray(p.followups)? p.followups: []).map(x=>String(x??''));
  p.widgets  = (Array.isArray(p.widgets)? p.widgets: []).map(w=>({name:String(w?.name||''), value:(typeof w?.value==='number'||typeof w?.value==='string')? w.value : '', unit:String(w?.unit||'')}));
  p.tasks    = (Array.isArray(p.tasks)? p.tasks: []).map((t,i)=>({ id:t?.id??(i+1), title:String(t?.title||''), detail:String(t?.detail||''), status:['todo','doing','done'].includes(String(t?.status))?t.status:'todo', category:String(t?.category||''), owner:String(t?.owner||'me'), eta_min:(typeof t?.eta_min==='number')?t.eta_min:(parseInt(t?.eta_min,10)||'') }));
  p.itinerary= (Array.isArray(p.itinerary)? p.itinerary: []).map(it=>({ date:String(it?.date||''), title:String(it?.title||''), start:String(it?.start||''), end:String(it?.end||''), duration_min:(typeof it?.duration_min==='number')?it.duration_min:(it?.start&&it?.end?undefined:30), location:String(it?.location||''), notes:String(it?.notes||'') }));
}

/************
 * Flows (Chat tab helper)
 ************/
async function llmFlow(text){
  const sys = `Return STRICT JSON: {"title":string,"steps":[{"id":1,"title":string,"detail":string,"status":"todo"|"doing"|"done"}...],"notes":string}`;
  const out = await askOpenAI([{role:'system', content:sys},{role:'user', content:text}], {temperature:0.2, json:true});
  return JSON.parse(out);
}

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

  renderProjectList();
  const pid = store.currentProjectId;
  if(pid && getProject(pid)) loadProjectIntoUI(getProject(pid));
  renderPlanFor(store.planDay);
})();
