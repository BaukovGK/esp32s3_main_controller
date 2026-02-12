/* Контроллер обратного осмоса — Dashboard */
const POLL = 2000;
let timer = null;
let manualMask = 0;

const DI_NAMES = ['Источ.','Промбак','Пром.полн','Перм.полн','E-STOP','Нас.1','Нас.2','Нас.3'];
const DO_NAMES = ['Нас.под.','Нас.1ст.','Нас.2ст.','ТЭН','Дозатор','','',''];
const ILK_NAMES = [
  'Аварийный стоп','Источник пуст','Промбак пуст',
  'P1 высокое','P3 высокое','P4 высокое',
  'Перегрев','dP фильтра',
  'Таймаут нас.1','Таймаут нас.2','Таймаут нас.3'
];
const STATE_CLS = {IDLE:'st-idle',AUTO:'st-auto',WASHING:'st-washing',MANUAL:'st-manual',FAULT:'st-fault'};

function $(id){return document.getElementById(id);}
function v(x){return x===null||x===undefined?'---':(typeof x==='number'?x.toFixed(x<10?2:1):''+x);}

/* Polling */
async function poll(){
  try {
    const r = await fetch('/api/v1/status');
    if(!r.ok) throw r.status;
    const d = await r.json();
    update(d);
    $('conn').className='led on';
  } catch(e){
    $('conn').className='led off';
  }
  /* Аварии и MQTT — параллельно */
  pollAlarms();
  pollMqtt();
}

function start(){poll();timer=setInterval(poll,POLL);}

/* Update UI */
function update(d){
  /* Состояние */
  const se=$('v-state');
  se.textContent=d.state;
  se.className='big '+(STATE_CLS[d.state]||'');
  $('v-sub').textContent=d.auto_sub||d.wash_sub||'—';
  const fe=$('v-fault');
  fe.textContent=d.fault_flags||'Нет';
  if(d.fault_flags) fe.style.color='var(--err)'; else fe.style.color='';

  /* Аналоговые */
  renderAnalog(d.analog||[]);
  /* Расход */
  renderFlow(d.flow||[]);
  /* Кондуктометры */
  renderCond(d.cond||[]);
  /* Телеметрия */
  renderTelem(d.telemetry||{});
  /* DI/DO */
  renderBits('v-di',d.di||0,DI_NAMES);
  renderBits('v-do',d.do||0,DO_NAMES);
  /* Блокировки */
  renderIlk(d.interlocks||{});
  /* Дозатор */
  $('v-doser-st').textContent=d.doser?d.doser.state:'---';
  const db=$('btn-doser');
  if(d.doser){
    db.textContent=d.doser.enabled?'ВКЛ':'ВЫКЛ';
    db.className='btn small '+(d.doser.enabled?'green':'red');
  }
  /* Ручное */
  const ms=$('sec-manual');
  if(d.state==='MANUAL'){
    ms.style.display='';
    manualMask=d.do||0;
    renderManual();
  } else {
    ms.style.display='none';
  }
  /* Uptime */
  const s=d.uptime_s||0;
  const dd=Math.floor(s/86400),hh=Math.floor(s%86400/3600),mm=Math.floor(s%3600/60);
  $('v-uptime').textContent='Uptime: '+dd+'д '+hh+'ч '+mm+'м';
}

function renderAnalog(arr){
  const g=$('g-analog');
  g.innerHTML='';
  arr.forEach(a=>{
    const c=document.createElement('div');
    c.className='card'+(a.fault?' fault':'');
    c.innerHTML='<label>'+a.name+'</label><span class="val">'+v(a.value)+'</span><span class="unit">'+a.unit+'</span>';
    g.appendChild(c);
  });
}

function renderFlow(arr){
  const g=$('g-flow');
  g.innerHTML='';
  arr.forEach(f=>{
    const c=document.createElement('div');
    c.className='card'+(f.ok?'':' fault');
    c.innerHTML='<label>'+f.name+'</label><span class="val">'+v(f.flow)+'</span><span class="unit">м³/ч</span><span class="sub">V='+v(f.volume)+' м³</span>';
    g.appendChild(c);
  });
}

function renderCond(arr){
  const g=$('g-cond');
  g.innerHTML='';
  arr.forEach(c=>{
    const d=document.createElement('div');
    d.className='card'+(c.ok?'':' fault');
    d.innerHTML='<label>'+c.name+'</label><span class="val">'+v(c.value)+'</span><span class="unit">мкСм/см</span><span class="sub">T='+v(c.temp)+' °C</span>';
    g.appendChild(d);
  });
}

function renderTelem(t){
  const g=$('g-telem');
  const items=[
    ['dP фильтра',t.filter_dp,'бар'],
    ['Подача 1-й',t.stage1_feed,'м³/ч'],
    ['Извлеч. 2-й',t.recovery2,'%'],
    ['Извлеч. общ.',t.recovery_sys,'%'],
    ['Селект. 1-й',t.sel1,'%'],
    ['Селект. 2-й',t.sel2,'%']
  ];
  g.innerHTML='';
  items.forEach(([n,val,u])=>{
    const c=document.createElement('div');
    c.className='card';
    c.innerHTML='<label>'+n+'</label><span class="val">'+v(val)+'</span><span class="unit">'+u+'</span>';
    g.appendChild(c);
  });
}

function renderBits(id,mask,names){
  const el=$(id);
  el.innerHTML='';
  for(let i=0;i<8;i++){
    const b=document.createElement('span');
    b.className='bit '+((mask>>i)&1?'on':'off');
    b.textContent=i+1;
    b.title=names[i]||('DI'+(i+1));
    el.appendChild(b);
  }
}

function renderIlk(ilk){
  const g=$('g-interlocks');
  g.innerHTML='';
  const flags=ilk.flags||0;
  ILK_NAMES.forEach((name,i)=>{
    const active=(flags>>i)&1;
    const d=document.createElement('div');
    d.className='ilk-item';
    d.innerHTML='<span class="dot '+(active?'active':'ok')+'"></span>'+name;
    g.appendChild(d);
  });
}

function renderManual(){
  const g=$('g-manual');
  g.innerHTML='';
  for(let i=0;i<8;i++){
    const on=(manualMask>>i)&1;
    const d=document.createElement('div');
    d.className='manual-item';
    const name=DO_NAMES[i]||('RO'+(i+1));
    d.innerHTML=name+' <button class="toggle'+(on?' on':'')+'" onclick="toggleDO('+i+')"></button>';
    g.appendChild(d);
  }
}

/* Аварии */
async function pollAlarms(){
  try {
    const r=await fetch('/api/v1/alarms');
    if(!r.ok) return;
    const d=await r.json();
    renderAlarms(d.active||[]);
  } catch(e){}
}

function renderAlarms(arr){
  const g=$('g-alarms');
  const empty=$('alarm-empty');
  const badge=$('alarm-cnt');
  g.innerHTML='';
  if(!arr.length){
    empty.style.display='';
    badge.style.display='none';
    return;
  }
  empty.style.display='none';
  badge.textContent=arr.length;
  badge.style.display='';
  arr.forEach(a=>{
    const d=document.createElement('div');
    d.className='alarm-item cat-'+a.cat;
    const ts=a.ts?new Date(a.ts/1000).toLocaleTimeString():'';
    d.innerHTML='<span class="alarm-cat">'+a.cat+'</span>'
      +'<span class="alarm-time">'+ts+'</span>'
      +'<span>'+a.code+(a.value?' ('+a.value.toFixed(1)+')':'')+'</span>';
    g.appendChild(d);
  });
}

/* MQTT статус */
async function pollMqtt(){
  try {
    const r=await fetch('/api/v1/mqtt/status');
    if(!r.ok) return;
    const d=await r.json();
    const led=$('mqtt-led');
    if(led) led.className='led '+(d.connected?'on':'off');
  } catch(e){}
}

/* Диагностика (раз в 10с) */
let diagTimer=null;
async function pollDiag(){
  try {
    const r=await fetch('/api/v1/diagnostics');
    if(!r.ok) return;
    const d=await r.json();
    renderDiag(d);
  } catch(e){}
}

function renderDiag(d){
  const g=$('g-diag');
  if(!g) return;
  const items=[
    ['Heap свобод.',fmtBytes(d.heap_free),''],
    ['Heap мин.',fmtBytes(d.heap_min),''],
    ['Uptime',fmtUptime(d.uptime_s),'']
  ];
  /* Стеки задач */
  if(d.stack){
    for(const [name,val] of Object.entries(d.stack)){
      items.push(['Стек '+name,val+' Б','']);
    }
  }
  /* Modbus */
  if(d.modbus){
    const addrs=['Sl.1','Sl.2','Sl.10','Sl.11'];
    d.modbus.online.forEach((on,i)=>{
      items.push(['MB '+addrs[i],(on?'OK':'OFFLINE')+(d.modbus.errors[i]?' ('+d.modbus.errors[i]+' ош.)':''),'']);
    });
  }
  g.innerHTML='';
  items.forEach(([n,val,u])=>{
    const c=document.createElement('div');
    c.className='card';
    c.innerHTML='<label>'+n+'</label><span class="val" style="font-size:14px">'+val+'</span>';
    g.appendChild(c);
  });
}

function fmtBytes(b){
  if(b>=1024*1024) return (b/1024/1024).toFixed(1)+' МБ';
  if(b>=1024) return (b/1024).toFixed(1)+' КБ';
  return b+' Б';
}
function fmtUptime(s){
  if(!s) return '---';
  const dd=Math.floor(s/86400),hh=Math.floor(s%86400/3600),mm=Math.floor(s%3600/60);
  return dd+'д '+hh+'ч '+mm+'м';
}

/* Команды */
async function sendCmd(cmd){
  try {
    const r=await fetch('/api/v1/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({cmd})});
    const d=await r.json();
    if(!d.ok) alert(d.error||'Ошибка');
    poll();
  } catch(e){alert('Нет связи');}
}

async function toggleDoser(){
  const cur=$('btn-doser').textContent==='ВКЛ';
  try {
    await fetch('/api/v1/doser/enable',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:!cur})});
    poll();
  } catch(e){}
}

async function toggleDO(bit){
  manualMask^=(1<<bit);
  try {
    await fetch('/api/v1/manual/do',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mask:manualMask})});
    poll();
  } catch(e){}
}

/* Настройки */
async function loadConfig(){
  try {
    const r=await fetch('/api/v1/config');
    const c=await r.json();
    $('c-p1').value=c.pressure.p1_max;
    $('c-p3').value=c.pressure.p3_max;
    $('c-p4').value=c.pressure.p4_max;
    $('c-dp').value=c.pressure.filter_dp_warn;
    $('c-drun').value=c.doser.run_time_min;
    $('c-dcyc').value=c.doser.cycle_time_min;
    $('c-wt').value=c.washing.target_temp_C;
    $('c-wm').value=c.washing.max_temp_C;
    $('c-wo').value=c.washing.t_overshoot_C;
    $('c-tconf').value=c.timeouts.pump_confirm_ms;
    $('c-tramp').value=c.timeouts.pump_ramp_ms;
    if(c.mqtt){
      $('c-mqtt-uri').value=c.mqtt.broker_uri||'';
      $('c-mqtt-user').value=c.mqtt.username||'';
      $('c-mqtt-pass').value=c.mqtt.password||'';
      $('c-mqtt-id').value=c.mqtt.client_id||'';
      $('c-mqtt-intv').value=c.mqtt.publish_interval_s||5;
      $('c-mqtt-en').checked=!!c.mqtt.enabled;
    }
  } catch(e){}
}

async function saveCfg(section){
  let body={};
  switch(section){
    case 'pressure':
      body={p1_max:+$('c-p1').value,p3_max:+$('c-p3').value,p4_max:+$('c-p4').value,filter_dp_warn:+$('c-dp').value};
      break;
    case 'doser':
      body={run_time_min:+$('c-drun').value,cycle_time_min:+$('c-dcyc').value};
      break;
    case 'washing':
      body={target_temp_C:+$('c-wt').value,max_temp_C:+$('c-wm').value,t_overshoot_C:+$('c-wo').value};
      break;
    case 'timeouts':
      body={pump_confirm_ms:+$('c-tconf').value,pump_ramp_ms:+$('c-tramp').value};
      break;
    case 'mqtt':
      body={broker_uri:$('c-mqtt-uri').value,username:$('c-mqtt-user').value,password:$('c-mqtt-pass').value,client_id:$('c-mqtt-id').value,publish_interval_s:+$('c-mqtt-intv').value,enabled:$('c-mqtt-en').checked?1:0};
      break;
  }
  try {
    const r=await fetch('/api/v1/config/'+section,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const d=await r.json();
    if(d.ok) alert('Сохранено!'); else alert(d.error||'Ошибка');
  } catch(e){alert('Нет связи');}
}

/* Запуск */
loadConfig();
start();
pollDiag();
diagTimer=setInterval(pollDiag,10000);
