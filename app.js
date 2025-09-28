/********************
 * Utility helpers  *
 ********************/
window.addEventListener('DOMContentLoaded', ()=>{ lucide.createIcons(); });

const $ = (s)=>document.querySelector(s);
const $$ = (s)=>document.querySelectorAll(s);

function escapeHtml(s){ return (s||'').replace(/[&<>"']/g, m=>({ '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;', "'":'&#39;' }[m])); }

/********************
 * Persistent store *
 ********************/
const store = {
  get key(){ return localStorage.getItem('OPENAI_KEY') || ''; },
  set key(v){ localStorage.setItem('OPENAI_KEY', v); },

  get chatHistory(){ return JSON.parse(localStorage.getItem('CHAT_HISTORY') || '[]'); },
  set chatHistory(v){ localStorage.setItem('CHAT_HISTORY', JSON.stringify(v)); },

  // Per-day plans: { "YYYY-MM-DD": { day, items:[...] } }
  get plans(){ return JSON.parse(localStorage.getItem('PLANS_BY_DAY') || '{}'); },
  set plans(v){ localStorage.setItem('PLANS_BY_DAY', JSON.stringify(v)); },

  get planDay(){ return localStorage.getItem('PLAN_DAY') || dayjs().format('YYYY-MM-DD'); },
  set planDay(v){ localStorage.setItem('PLAN_DAY', v); },
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
  // Default to PLAN
  document.querySelector('[data-tab="plan"]').click();

  // API Key controls
  $('#apiKey').value = store.key;
  $('#saveKey').onclick = ()=>{ store.key = $('#apiKey').value.trim(); alert('Saved!'); };
  $('#clearKey').onclick = ()=>{ localStorage.removeItem('OPENAI_KEY'); $('#apiKey').value=''; alert('Cleared!'); };

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
      bubble.innerHTML = '<div class="text-[11px] uppercase tracking-wide text-slate-500 mb-1">'+(me?'You':'Assistant')+'</div><div class="whitespace-pre-wrap">'+escapeHtml(m.content)+'</div>';
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
async function askOpenAI(messages, {model='gpt-4o-mini', temperature=0.2, json=false} = {}){
  const key = store.key;
  if(!key) throw new Error('Missing OpenAI API key. Click Save at the top.');
  const body = { model, temperature, messages };
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

function emptyPlan(dayIso){ return { day: dayIso, items: [] }; }

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

// Local fallback (only used if OpenAI fails)
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

    if(parsed.length && parsed[0].start && parsed[0].end){
      const s = parsed[0].start.date(), e = parsed[0].end.date();
      items.push({ title:titleWithCompanion(), type:'fixed',
        start:dayjs(s).format('HH:mm'), end:dayjs(e).format('HH:mm'),
        duration_min: dayjs(e).diff(dayjs(s),'minute'), notes:'' });
      continue;
    }
    if(parsed.length && parsed[0].start){
      const s = parsed[0].start.date(); const d = durMin || defaultDur(line);
      items.push({ title:titleWithCompanion(), type:'fixed',
        start:dayjs(s).format('HH:mm'), end:dayjs(s).add(d,'minute').format('HH:mm'),
        duration_min:d, notes:'' });
      continue;
    }
    const d = durMin || defaultDur(line);
    const isMeal = /\b(breakfast|lunch|dinner)\b/i.test(line);
    items.push({ title:titleWithCompanion(), type: isMeal ? 'meal' : 'flexible', start:'', end:'', duration_min:d, notes:'' });
  }
  const sleepM = text.match(/sleep\s+by\s+([0-9:.\sapm]+)/i);
  if(sleepM){
    const tParsed = chrono.parse('at '+sleepM[1], new Date());
    if(tParsed[0]?.start){
      const e = dayjs(tParsed[0].start.date()).format('HH:mm');
      items.push({title:'Sleep', type:'rest', start:'', end:e, duration_min:0, notes:''});
    }
  }
  return { items };
}

// AI-first parsing with strict JSON schema & examples
async function aiDraft(text, baseDayIso){
  const sys = `
You are a scheduling parser. Convert messy natural text into STRICT JSON:

{
  "day": "YYYY-MM-DD",
  "items": [
    {"title": "Breakfast with Kobe", "type": "fixed"|"flexible"|"meal"|"rest", "start":"HH:mm","end":"HH:mm","duration_min":0,"notes":""}
  ]
}

Rules:
- Assume date ${baseDayIso} unless another date is clearly stated.
- Understand sloppy ranges: "9-9:50am" -> 09:00–09:50 (fill missing am/pm by context).
- "~1" ≈ 13:00 if it's lunch; keep reasonable assumptions.
- Preserve companions/locations in title or notes ("with Kobe", "at De Neve").
- If no time given for meals: default durations breakfast 30m, lunch 45m, dinner 45m.
- "Sleep by 11" -> rest item ending at 23:00.
- Chores without time but with duration -> flexible with duration_min.
- Output MUST be valid JSON. No extra text.`;

  const fewshotUser = `Examples:
- "tomorrow I'll have breakfast from 9-9:50am with Kobe at De Neve"
- "physics 9–11 in Boelter 3400; lunch with Maya ~1 for 45m; meditate; cut nails (10m); sleep by 11"`;

  const fewshotAssistant = JSON.stringify({
    day: baseDayIso,
    items: [
      { title: "Breakfast with Kobe", type: "meal", start: "09:00", end: "09:50", duration_min: 50, notes: "De Neve" },
      { title: "Physics (Boelter 3400)", type: "fixed", start: "09:00", end: "11:00", duration_min: 120, notes: "" },
      { title: "Lunch with Maya", type: "meal", start: "13:00", end: "13:45", duration_min: 45, notes: "" },
      { title: "Meditate", type: "flexible", start: "", end: "", duration_min: 20, notes: "" },
      { title: "Cut nails", type: "flexible", start: "", end: "", duration_min: 10, notes: "" },
      { title: "Sleep", type: "rest", start: "", end: "23:00", duration_min: 0, notes: "" }
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

// ✅ Allocate: ANY item with start+end is treated as FIXED (keeps its time)
function allocatePlan(baseDayIso, draft){
  const startOfDay = dayjs(baseDayIso + 'T06:30');
  const endOfDay   = dayjs(baseDayIso + 'T24:00');
  const toTime = (hhmm)=>dayjs(baseDayIso + 'T' + hhmm.padStart(5,'0'));

  const fixed = [], flex = [];

  for (const raw of (draft.items || [])) {
    const it = { ...raw };
    if (it.start && it.end) {
      fixed.push({ ...it, startDT: toTime(it.start), endDT: toTime(it.end) });
      continue;
    }
    let d = Number(it.duration_min) || 0;
    if (it.type === 'meal' && !d) {
      const t = (it.title || '').toLowerCase();
      d = t.includes('breakfast') ? 30 : (t.includes('lunch') || t.includes('dinner')) ? 45 : 30;
    }
    flex.push({ ...it, duration_min: d || 30 });
  }

  fixed.sort((a,b)=>a.startDT - b.startDT);

  const gaps = [];
  let cursor = startOfDay;
  for (const f of fixed) {
    if (f.startDT.isAfter(cursor)) gaps.push({ start: cursor, end: f.startDT });
    if (f.endDT.isAfter(cursor))   cursor = f.endDT;
  }
  if (cursor.isBefore(endOfDay)) gaps.push({ start: cursor, end: endOfDay });

  const placed = [...fixed];
  function placeOne(task){
    for (const g of gaps) {
      const freeMin = g.end.diff(g.start, 'minute');
      if (freeMin >= task.duration_min) {
        task.startDT = g.start;
        task.endDT   = g.start.add(task.duration_min, 'minute');
        g.start = task.endDT;
        placed.push(task);
        return true;
      }
    }
    return false;
  }
  for (const t of flex) placeOne(t);

  let id = 1;
  const items = placed.map(it => {
    const start = it.startDT ? it.startDT.format('HH:mm') : (it.start || '');
    const end   = it.endDT   ? it.endDT.format('HH:mm')   : (it.end   || '');
    const dur   = it.duration_min || (start && end ? dayjs(baseDayIso+'T'+end).diff(dayjs(baseDayIso+'T'+start),'minute') : 0);
    return {
      id: id++,
      title: it.title || 'Task',
      type:  it.type  || (start && end ? 'fixed' : 'flexible'),
      start, end, duration_min: dur, notes: it.notes || ''
    };
  }).sort((a,b)=>a.start.localeCompare(b.start));

  return { day: baseDayIso, items };
}

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

/***************
 * Rendering   *
 ***************/
function renderPlanFor(day){
  store.planDay = day;
  const badge = $('#planDayBadge'); const label = $('#currentDayLabel');
  if(badge) badge.textContent = 'Day: ' + day;
  if(label) label.textContent = dayjs(day).format('ddd, MMM D, YYYY');
  renderPlan(getPlan(day));
}

function renderPlan(plan){
  const wrap = $('#planTable');
  if(!wrap) return;
  if(!plan){ wrap.innerHTML = '<div class="text-slate-500">No plan yet. Add items and click “Make / Update Plan”.</div>'; renderTimeline(null); return; }

  const rows = plan.items.map(it=>`
    <div class="grid grid-cols-12 items-center gap-2 py-1 border-b border-slate-100">
      <input class="col-span-4 card !py-1" data-edit="title" data-id="${it.id}" value="${escapeHtml(it.title)}" />
      <select class="col-span-2 card !py-1" data-edit="type" data-id="${it.id}">
        ${['fixed','flexible','meal','rest'].map(t=>`<option ${it.type===t?'selected':''}>${t}</option>`).join('')}
      </select>
      <input class="col-span-2 card !py-1" data-edit="start" data-id="${it.id}" value="${it.start}" placeholder="HH:mm"/>
      <input class="col-span-2 card !py-1" data-edit="end" data-id="${it.id}" value="${it.end}" placeholder="HH:mm"/>
      <input class="col-span-2 card !py-1" data-edit="notes" data-id="${it.id}" value="${escapeHtml(it.notes||'')}" placeholder="notes"/>
    </div>
  `).join('');

  wrap.innerHTML = `
    <div class="grid grid-cols-12 gap-2 text-xs text-slate-500 pb-1">
      <div class="col-span-4">Title</div>
      <div class="col-span-2">Type</div>
      <div class="col-span-2">Start</div>
      <div class="col-span-2">End</div>
      <div class="col-span-2">Notes</div>
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
    p.items.push({id:(p.items.at(-1)?.id||0)+1, title:'New Task', type:'flexible', start:'', end:'', duration_min:30, notes:''});
    setPlan(day, p); renderPlan(p);
  });

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
      block.innerHTML = `<div class="font-semibold text-[13px]">${escapeHtml(it.title)}</div><div class="opacity-90">${it.start}–${it.end}</div>`;
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
 * Planner: actions    *
 ***********************/
$('#makePlan')?.addEventListener('click', async ()=>{
  const raw = $('#planInput').value.trim();
  if(!raw) return;
  const baseDay = detectBaseDay(raw);
  store.planDay = baseDay;

  try{
    // AI-first structuring
    const ai = await aiDraft(raw, baseDay);

    // Keep user-edited fixed items from prior plan for that day
    const prior = getPlan(baseDay);
    let toAllocate = ai;
    if(prior){
      const locked = prior.items.filter(it=>it.start && it.end);
      toAllocate = { day: baseDay, items: [
        ...locked.map(it=>({title:it.title, type:it.type, start:it.start, end:it.end, duration_min:it.duration_min||0, notes:it.notes||''})),
        ...ai.items
      ]};
    }

    const finalPlan = allocatePlan(baseDay, toAllocate);
    setPlan(baseDay, finalPlan);
    renderPlanFor(baseDay);
    scheduleLocalNotifications(finalPlan);

  }catch(err){
    // Fallback to local parsing if OpenAI fails
    console.warn('AI parse failed, falling back to local parser:', err);
    const draft = localDraft(raw);
    const finalPlan = allocatePlan(baseDay, draft);
    setPlan(baseDay, finalPlan);
    renderPlanFor(baseDay);
  }
});

$('#resetPlan')?.addEventListener('click', ()=>{
  const day = store.planDay;
  const all = store.plans; delete all[day]; store.plans = all;
  renderPlanFor(day);
});

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
    cal.addEvent(it.title, it.notes||'', '',
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
      setTimeout(()=>{ new Notification('Upcoming: '+it.title, { body: `${it.start}–${it.end} ${it.notes||''}` }); }, ms);
    }
  }
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
        <div class="font-semibold">${escapeHtml(s.title)}</div>
        <div class="text-sm text-slate-600 whitespace-pre-wrap">${escapeHtml(s.detail)}</div>
        <div class="mt-1">
          <select data-stat="${s.id}" class="card !py-1">
            ${['todo','doing','done'].map(st=>`<option ${s.status===st?'selected':''}>${st}</option>`).join('')}
          </select>
        </div>
      </div>
    </div>`).join('');
  wrap.innerHTML = `
    <div class="mb-2">
      <textarea id="flowNotes" class="w-full card !py-2 mt-2" rows="2" placeholder="Notes...">${escapeHtml(flow.notes||'')}</textarea>
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
  renderPlanFor(store.planDay);
  const savedFlow = JSON.parse(localStorage.getItem('FLOW_V1')||'null'); renderFlow(savedFlow);
})();
