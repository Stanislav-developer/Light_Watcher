#include "lw.h"

static bool pubIpTried = false;
static bool pubIpOk = false;
static uint8_t admSalt[16];
static uint8_t admHash[32];
static uint32_t admIter = ADM_ITER;
static String webSess[WEB_SESS_MAX];
static WebFailEntry webFails[WEB_FAIL_SLOTS];
static String webPubCache;
static String webPubLite;
static unsigned long webPubLiteMs = 0;

const char lw_page_html[] PROGMEM = R"lwpage(<!DOCTYPE html><html lang="uk"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="light dark"><title>Light Watcher</title>
<link rel="icon" type="image/png" sizes="64x64" href="favicon.png">
<style>
:root{--bg:#0f141a;--card:#18202a;--ink:#e6ebf1;--mut:#8e9bab;--line:#263140;--on:#33c072;--onS:#14301f;--off:#ef5f5f;--offS:#3a1b1d;--warn:#f0a83c;--warnS:#382a14;--nod:#2c3644;--acc:#6f9bff;--accS:#1b2a45;--sh:none;color-scheme:dark}
@media(prefers-color-scheme:light){:root:not([data-theme=dark]){--bg:#f2f4f7;--card:#fff;--ink:#17202b;--mut:#647386;--line:#e6eaf0;--on:#1f9d55;--onS:#e3f5ea;--off:#d64545;--offS:#fbe7e7;--warn:#c27a12;--warnS:#fdf1de;--nod:#d3d9e1;--acc:#2a62d4;--accS:#e5edfb;--sh:0 1px 3px rgba(20,35,60,.08),0 4px 16px rgba(20,35,60,.05);color-scheme:light}}
:root[data-theme=light]{--bg:#f2f4f7;--card:#fff;--ink:#17202b;--mut:#647386;--line:#e6eaf0;--on:#1f9d55;--onS:#e3f5ea;--off:#d64545;--offS:#fbe7e7;--warn:#c27a12;--warnS:#fdf1de;--nod:#d3d9e1;--acc:#2a62d4;--accS:#e5edfb;--sh:0 1px 3px rgba(20,35,60,.08),0 4px 16px rgba(20,35,60,.05);color-scheme:light}
*{box-sizing:border-box}html,body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.4 -apple-system,"Segoe UI",Roboto,Arial,sans-serif}
.w{max-width:760px;margin:0 auto;padding:12px 16px 40px}a{color:var(--acc)}
.top{display:flex;align-items:center;gap:10px;padding:6px 2px 12px}.top h1{font-size:17px;margin:0}.ti{min-width:0;flex:1}.loc{color:var(--mut);font-size:13px;margin-top:1px}
.tg{margin-left:auto;background:none;border:1px solid var(--line);color:var(--mut);border-radius:8px;padding:4px 9px;font-size:13px;cursor:pointer}
.c{background:var(--card);border-radius:16px;box-shadow:var(--sh);padding:16px;margin-bottom:12px}
.stale{background:var(--offS);color:var(--off);border-radius:12px;padding:10px 14px;margin-bottom:12px;font-size:14px}
.hero{padding:20px}.hero.on{background:linear-gradient(180deg,var(--onS),var(--card) 75%)}.hero.off{background:linear-gradient(180deg,var(--offS),var(--card) 75%)}
.st{font-size:15px;font-weight:600;display:flex;align-items:center;gap:8px}.st i{width:12px;height:12px;border-radius:50%;display:inline-block}i.on{background:var(--on)}i.off{background:var(--off)}
.dur{font-size:40px;font-weight:750;letter-spacing:-.02em;margin:4px 0 2px;line-height:1.15}
.sub,.l,.mut{color:var(--mut)}.sub{font-size:14px}.l{font-size:12px}.mut{font-size:13px}.err{color:var(--off)}
.row{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:16px}
.box{border:1px solid var(--line);border-radius:12px;padding:10px 12px;background:var(--card)}
.box .v{font-size:18px;font-weight:700;margin:2px 0}.box .v.sm{font-size:15px}
.vbar{position:relative;height:6px;border-radius:3px;background:var(--line);margin-top:8px}
.vbar b{position:absolute;top:0;bottom:0;background:var(--onS);border-left:2px solid var(--on);border-right:2px solid var(--on)}
.vbar u{position:absolute;top:-3px;width:3px;height:12px;margin-left:-1px;border-radius:2px;background:var(--ink)}
.pill{display:inline-block;font-size:11px;font-weight:600;padding:1px 7px;border-radius:20px;margin-left:5px}
.pill.r{background:var(--offS);color:var(--off)}.pill.a{background:var(--warnS);color:var(--warn)}
.dev{margin-top:10px;font-size:13px;color:var(--warn)}
h2{font-size:15px;margin:0}.hd{display:flex;align-items:center;gap:8px;margin-bottom:8px;flex-wrap:wrap}
.seg{margin-left:auto;display:flex;background:var(--bg);border-radius:9px;padding:2px}
.seg button{border:0;background:none;color:var(--mut);font-size:13px;padding:5px 10px;border-radius:7px;cursor:pointer}
.seg button.a{background:var(--card);color:var(--ink);box-shadow:var(--sh);font-weight:600}
.ban{border-left:4px solid var(--off);background:var(--offS);color:var(--off);border-radius:8px;padding:8px 10px;margin:8px 0;font-size:13px}.ban.a{border-color:var(--warn);background:var(--warnS);color:var(--warn)}
.ln{display:grid;grid-template-columns:38px 1fr;align-items:center;gap:8px;margin:5px 0}.ln>span{font-size:12px;color:var(--mut)}
.tr{position:relative;background:var(--nod);border-radius:6px;overflow:hidden;height:30px}.tr.p{height:12px}
.tr b{position:absolute;top:0;bottom:0;cursor:pointer}.bon{background:var(--on)}.boff{background:var(--off)}.bpos{background:var(--warn)}.bnod{background:var(--nod)}
.tw{position:relative}.now{position:absolute;top:-4px;bottom:-4px;width:2px;margin-left:-1px;background:var(--ink);pointer-events:none}
.ax{display:grid;grid-template-columns:38px 1fr;gap:8px}.ax div{position:relative;height:16px;font-size:11px;color:var(--mut)}.ax em{position:absolute;transform:translateX(-50%);font-style:normal}.ax em:first-child{transform:none}.ax em:last-child{transform:translateX(-100%)}
.tip{min-height:20px;font-size:13px;color:var(--mut);margin-top:6px}.tr.dim,.slt.dim{opacity:.6}.slt{margin-top:14px;padding-top:12px;border-top:1px solid var(--line)}.chips{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px}.chip{background:var(--offS);color:var(--off);border-radius:10px;padding:6px 10px;font-size:14px;font-weight:650}.chip.a{background:var(--warnS);color:var(--warn)}.chip small{font-weight:500;opacity:.8;margin-left:6px;font-size:12px}.tip b{color:var(--ink)}
.lg{display:flex;flex-wrap:wrap;gap:4px 14px;font-size:12px;color:var(--mut);margin-top:8px}.lg i{display:inline-block;width:10px;height:10px;border-radius:3px;margin-right:5px;vertical-align:-1px}
.tabs{display:flex;gap:4px;overflow-x:auto;margin:16px 0 10px;scrollbar-width:none}
.tabs button{flex:none;border:0;background:none;color:var(--mut);font-size:14px;padding:8px 12px;border-radius:10px;cursor:pointer}
.tabs button.a{background:var(--card);color:var(--ink);font-weight:650;box-shadow:var(--sh)}
.g{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.k{background:var(--bg);border-radius:12px;padding:10px 12px;min-width:0}.k .v{font-size:18px;font-weight:700;margin-top:2px}
.wk .ln,.wk .ax{grid-template-columns:30px 1fr}.wk .tr{height:18px}
.mk{position:absolute;top:0;bottom:0;width:3px;margin-left:-1px;background:var(--acc);pointer-events:none}.mk.s{background:#a78bfa}
table{width:100%;border-collapse:collapse;font-size:14px}td,th{padding:8px 2px;border-bottom:1px solid var(--line);text-align:left;vertical-align:top}th{color:var(--mut);font-weight:600;font-size:13px}td.r{text-align:right;color:var(--mut)}
.day{font-size:12px;font-weight:700;color:var(--mut);text-transform:uppercase;letter-spacing:.04em;margin:14px 0 2px}
.sc{overflow-x:auto}.lw{color:var(--warn)}.le{color:var(--off)}
.lgf{display:flex;gap:6px;margin-bottom:8px;flex-wrap:wrap}.lgf button{border:1px solid var(--line);background:none;color:var(--mut);border-radius:20px;padding:4px 10px;font-size:12px;cursor:pointer}.lgf button.a{background:var(--accS);color:var(--acc);border-color:transparent}
button.btn{border:0;background:var(--acc);color:#fff;border-radius:10px;padding:10px 16px;font-size:14px;cursor:pointer;font-weight:600}button.s{background:var(--accS);color:var(--acc)}
input{width:100%;padding:10px;border:1px solid var(--line);border-radius:10px;font-size:16px;margin:4px 0 10px;background:var(--bg);color:var(--ink)}
details summary{cursor:pointer;color:var(--acc);margin-top:12px}
svg{display:block;width:100%;height:auto}svg text{fill:var(--mut)}svg .vb{fill:var(--acc);cursor:pointer}svg .vo{fill:var(--off);cursor:pointer}svg .vz{fill:var(--onS)}
@media(max-width:560px){.dur{font-size:34px}.g{grid-template-columns:1fr 1fr}.row{grid-template-columns:1fr}.c{padding:14px}}
</style></head><body><div class="w"><div class="top"><div class="ti"><h1>Light Watcher</h1><div class="loc" id="loc"></div></div><button class="tg" id="th" title="Тема як у системі">◐</button></div>
<div id="stale"></div><div id="app"><div class="c mut">Завантаження…</div></div></div>
<script>
var D=null,SYS=null,RCV=0,FAIL=0,SK=0,EV=null,SG=null,BUSY=0,TM=0,LAST={},TZ='Europe/Kyiv',S={tab:'ov',d:0,w:0,lf:0,sp:0,tip:'',wt:'',vt:''},VB=[];
function $(i){return document.getElementById(i)}
function esc(s){return String(s==null?'':s).replace(/[&<>"']/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]})}
var FC={};function fmt(l,o){var k=l+JSON.stringify(o);if(!FC[k]){o.timeZone=TZ;FC[k]=new Intl.DateTimeFormat(l,o)}return FC[k]}
function fm(t,o){try{return fmt('uk-UA',o).format(new Date(t*1000))}catch(e){return new Date(t*1000).toLocaleString('uk-UA')}}
function hm(t){return fm(t,{hour:'2-digit',minute:'2-digit',hourCycle:'h23'})}
function fd(t){return fm(t,{day:'numeric',month:'long',year:'numeric'}).replace(/\sр\.$/,'')}
function fdm(t){return fm(t,{day:'numeric',month:'long'})}
function fdIso(v){if(!/^\d{4}-\d{2}-\d{2}/.test(String(v||'')))return '';var p=String(v).slice(0,10).split('-');return fd(Date.UTC(+p[0],+p[1]-1,+p[2],12)/1000)}
function p2(n){return(n<10?'0':'')+n}
function mm(m){return p2(Math.floor(m/60))+':'+p2(m%60)}
function dur(s){s=Math.round(s);if(s<60)return'менше 1 хв';var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+' д ':'')+(d||h?h+' год ':'')+m+' хв'}
function kp(l,v,c){return'<div class="k"><div class="l">'+l+'</div><div class="v"'+(c?' style="color:var(--'+c+')"':'')+'>'+v+'</div></div>'}
function row(a,b){return'<tr><td>'+a+'</td><td class="r">'+b+'</td></tr>'}
function set(id,h){var e=$(id);if(e&&LAST[id]!==h){LAST[id]=h;e.innerHTML=h;return 1}return 0}
function nowT(){return D.now?D.now+Math.floor((Date.now()-RCV)/1000):0}
function kyMin(t){try{var p=fmt('en-GB',{hour:'2-digit',minute:'2-digit',hourCycle:'h23'}).format(t*1000).split(':');return(+p[0])*60+(+p[1])}catch(e){var d=new Date(t*1000);return d.getHours()*60+d.getMinutes()}}
function kyDate(t){try{return fmt('en-CA',{year:'numeric',month:'2-digit',day:'2-digit'}).format(t*1000)}catch(e){return''}}
var TH='';function theme(t){var r=document.documentElement,b=$('th');TH=t==='light'||t==='dark'?t:'';if(TH)r.setAttribute('data-theme',TH);else r.removeAttribute('data-theme');b.textContent=TH==='light'?'☀︎':TH==='dark'?'☾':'◐';b.title=TH==='light'?'Світла тема':TH==='dark'?'Темна тема':'Тема як у системі'}
try{theme(localStorage.getItem('lwThemeM'))}catch(e){theme('')}
$('th').onclick=function(){theme(TH===''?'light':TH==='light'?'dark':'');try{if(TH)localStorage.setItem('lwThemeM',TH);else localStorage.removeItem('lwThemeM')}catch(e){}};
try{var h0=location.hash.slice(1);if(/^(ov|wk|vo|hi|dv)$/.test(h0))S.tab=h0}catch(e){}
function plan(){clearTimeout(TM);TM=setTimeout(function(){if(!document.hidden)load()},D&&!D.pw?60000:30000)}
function load(){if(BUSY)return;BUSY=1;clearTimeout(TM);fetch('api/data?ts='+Date.now()+(!D||D.pw?'&v=1':'')+(S.tab==='vo'?'':'&l=1'),{cache:'no-store',credentials:'same-origin'}).then(function(r){BUSY=0;
if(r.status==401){login('');return}if(r.status==403){nopass();return}if(!r.ok)throw 0;
return r.json().then(function(j){D=j;EV=SG=null;RCV=Date.now();FAIL=0;stale();render();plan();if(D.dev&&S.tab==='dv')loadSys()})}).catch(function(){BUSY=0;FAIL++;plan();if(D==null){if(!$('lf'))$('app').innerHTML='<div class="c err">Немає зв\'язку з пристроєм. Спробуйте оновити сторінку.</div>'}else stale()})}
document.addEventListener('visibilitychange',function(){if(!document.hidden&&D&&Date.now()-RCV>10000)load()});
function stale(){$('stale').innerHTML=FAIL&&D?'<div class="stale">⚠️ Немає зв\'язку з пристроєм'+(D.now?' з '+hm(nowT()):'')+'. Дані нижче можуть бути застарілими.</div>':''}
function reset(){D=null;SK=0;LAST={};$('loc').textContent='';$('stale').innerHTML=''}
function nopass(){reset();$('app').innerHTML='<div class="c"><h2>🔐 Адмінпанель</h2><p>Пароль адміністратора ще не створено.</p><p class="mut">Створіть його в Telegram-боті: ⚙️ Налаштування → 🔑 Адмінпанель. Потім оновіть цю сторінку.</p></div>'}
function login(m){reset();$('app').innerHTML='<div class="c"><h2 style="margin-bottom:10px">🔐 Вхід адміністратора</h2><form id="lf"><input id="u" placeholder="Логін" autocomplete="username" autocapitalize="none"><input id="p" type="password" placeholder="Пароль" autocomplete="current-password"><button class="btn">Увійти</button> <span id="lm" class="err">'+esc(m)+'</span></form></div>';
$('lf').onsubmit=function(e){e.preventDefault();var b='user='+encodeURIComponent($('u').value)+'&pass='+encodeURIComponent($('p').value);
fetch('api/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b,credentials:'same-origin'}).then(function(r){return r.json().then(function(j){
if(r.ok&&j.ok){$('app').innerHTML='<div class="c mut">Завантаження…</div>';BUSY=0;load()}else if(j.wait){$('lm').textContent='Забагато невдалих спроб. Зачекайте '+j.wait+' с.'}
else if(r.status==403){nopass()}else{$('lm').textContent='Невірний логін або пароль.'}})}).catch(function(){$('lm').textContent='Помилка зв\'язку.'})}}
function loadSys(){fetch('api/sys',{cache:'no-store',credentials:'same-origin'}).then(function(r){if(!r.ok)throw 0;return r.json()}).then(function(j){SYS=j;pane()}).catch(function(){})}
function evs(){if(!EV)EV=(D.ev||[]).filter(function(e){return e[0]>0}).sort(function(a,b){return a[0]-b[0]});return EV}
function cnt(since){var E=evs(),n=0;E.forEach(function(e){if(e[1]==3&&e[0]>=since)n++});return n+((D.evFull&&E.length&&E[0][0]>since)?'+':'')}
function segs(){if(SG)return SG;var o=[],cur=-2,ct=0;evs().forEach(function(e){var k=e[1],s=(k==1||k==4)?1:(k==2||k==3)?0:-1;if(s===cur&&s>=0)return;if(cur>=0&&e[0]>ct)o.push([ct,e[0],cur]);var l=o[o.length-1];if(cur<0&&s>=0&&l&&l[2]===s&&l[1]===ct&&e[0]-ct<=300){o.pop();cur=s;ct=l[0];return}cur=s;ct=e[0]});if(cur>=0&&D.now>ct)o.push([ct,D.now,cur]);return SG=o}
function clip(a,b){var o=[];segs().forEach(function(s){var x=Math.max(s[0],a),z=Math.min(s[1],b);if(z>x)o.push([x,z,s[2]])});return o}
function offIn(a,b){var n=0;clip(a,b).forEach(function(s){if(!s[2])n+=s[1]-s[0]});return n}
function onIn(a,b){var n=0;clip(a,b).forEach(function(s){if(s[2])n+=s[1]-s[0]});return n}
function today(){if(!D.wk)return 0;for(var i=0;i<14;i++)if(D.wk[i]<=D.now&&D.now<D.wk[i+1])return D.wk[i];return 0}
function vsamp(){return(D.vs||[]).filter(function(v){return v[0]>D.now-86400})}
function lastChange(){var E=evs();for(var i=E.length-1;i>=0;i--){var k=E[i][1];if(k>=1&&k<=4)return((k==4&&D.pw)||(k==3&&!D.pw))?E[i][0]:0}return 0}
function sl(a,merge){var o=[];(a||[]).forEach(function(x){var s=Math.max(0,Math.min(1440,+x.s||0)),e=Math.max(s,Math.min(1440,+x.e||0));if(e>s)o.push({s:s,e:e,t:x.t})});o.sort(function(a,b){return a.s-b.s});if(!merge)return o;
var m=[];o.forEach(function(x){var l=m[m.length-1];if(l&&x.s<=l.e){if(x.e>l.e)l.e=x.e;if(x.t!=='Possible')l.t=x.t}else m.push({s:x.s,e:x.e,t:x.t})});return m}
function todayOk(){var y=D.ys||{};return y.valid&&y.todayDate&&D.now&&kyDate(D.now)===String(y.todayDate).slice(0,10)}
var STX={ScheduleApplies:'Графік застосовується',WaitingForSchedule:'Очікується уточнення графіка',EmergencyShutdowns:'Екстрені відключення',PlannedShutdowns:'Аварійні відключення',NoShutdowns:'Відключень не заплановано'};
function render(){if(!SK){SK=1;LAST={};$('app').innerHTML='<div class="c hero" id="hero"></div><div class="c" id="day"></div><div class="tabs" id="tabs"></div><div id="pane"></div>'}
var y=D.ys||{},ad=[y.city,y.street,y.house].filter(function(x){return x}).join(', ');$('loc').textContent=y.configured?[ad,y.group?'група '+y.group:''].filter(function(x){return x}).join(' · '):'';
if(S.tab==='dv'&&!D.dev)S.tab='ov';hero();set('day',cDay());tabs();pane()}
function hero(){var on=!!D.pw;$('hero').className='c hero '+(on?'on':'off');set('hero',cHero())}
function cHero(){var on=!!D.pw,t=lastChange(),h='<div class="st"><i class="'+(on?'on':'off')+'"></i>'+(on?'Світло є':'Світла немає')+'</div>';
h+='<div class="dur">'+(t&&D.sync?dur(nowT()-t):esc(D.dur||'—'))+'</div><div class="sub">'+(D.since?'з '+esc(D.since):'')+(D.since&&D.prev?' · ':'')+(D.prev?'до цього '+(on?'світла не було ':'світло було ')+esc(D.prev):'')+'</div>';
h+='<div class="row"><div class="box">'+cVBox()+'</div><div class="box">'+cNext()+'</div></div>'+cDev();if(!D.sync)h+='<div class="dev">⚠️ Час на пристрої ще не синхронізовано.</div>';return h}
function cVBox(){var h='<div class="l">Напруга</div>';if(!D.pw)return h+'<div class="v sm mut">—</div><div class="l">без світла не вимірюється</div>';if(D.volt==null)return h+'<div class="v sm mut">—</div><div class="l">немає свіжих даних</div>';
var lo=D.lim[0],hi=D.lim[1],v=D.volt,a=Math.min(lo-15,v-5),b=Math.max(hi+15,v+5),pc=function(x){return(x-a)/(b-a)*100},out=v<lo||v>hi;
return h+'<div class="v"'+(out?' style="color:var(--off)"':'')+'>'+v.toFixed(1).replace('.',',')+' В</div><div class="vbar"><b style="left:'+pc(lo).toFixed(1)+'%;right:'+(100-pc(hi)).toFixed(1)+'%"></b><u style="left:'+pc(v).toFixed(1)+'%"></u></div><div class="l" style="margin-top:6px">межі '+lo+'–'+hi+' В · '+hm(D.voltT)+'</div>'}
function cNext(){var y=D.ys||{},h='<div class="l">Далі за графіком</div>';
if(!y.configured)return h+'<div class="v sm">Адресу не налаштовано</div>'+(D.dev?'<div class="l">Telegram → ⚙️ Налаштування → 📅 Адреса YASNO</div>':'');
if(!y.valid)return h+'<div class="v sm">Графік ще не завантажено</div>';
if(y.todayStatus==='EmergencyShutdowns')return h+'<div class="v sm err">🚨 Екстрені відключення</div><div class="l">Плановий графік зараз не діє</div>';
if(!D.sync||!todayOk())return h+'<div class="v sm">Немає графіка на сьогодні</div>';
var pp=kyMin(nowT()),A=sl(y.today,1),B=(y.tomorrowDate&&y.tomorrowStatus!=='EmergencyShutdowns')?sl(y.tomorrow,1):[],r=null;
for(var i=0;i<A.length&&!r;i++){var a=A[i];if(pp>=a.s&&pp<a.e){var e=a.e,tm=0;if(e>=1440&&B.length&&B[0].s===0){e=B[0].e;tm=1}r={on:1,at:e,n:(tm?1440:0)+e-pp,tm:tm,t:a.t}}else if(a.s>pp)r={on:0,at:a.s,n:a.s-pp,t:a.t}}
if(!r&&B.length)r={on:0,at:B[0].s,n:1440-pp+B[0].s,tm:1,t:B[0].t};
if(!r)return h+'<div class="v sm">Сьогодні відключень більше немає</div>';
h+='<div class="v">'+(r.on?'Світло о '+mm(r.at):'Відключення о '+mm(r.at))+(r.tm?' <span class="mut">завтра</span>':'')+'</div><div class="l">через '+dur(r.n*60);
if(!r.on)h+=r.t==='Possible'?'<span class="pill a">можливе</span>':'<span class="pill r">підтверджене</span>';
if(y.todayStatus==='WaitingForSchedule')h+='<span class="pill a">уточнюється</span>';return h+'</div>'}
function cDev(){var y=D.ys||{};if(!D.sync||!todayOk()||y.todayStatus==='EmergencyShutdowns'||y.todayStatus==='WaitingForSchedule')return'';
var pp=kyMin(nowT()),cur=null;sl(y.today,0).forEach(function(a){if(pp>=a.s&&pp<a.e&&(!cur||a.t!=='Possible'))cur=a});if(cur&&cur.t==='Possible')return'';
if(cur&&D.pw)return'<div class="dev">≠ Не за графіком: за планом зараз відключення, але світло є</div>';if(!cur&&!D.pw)return'<div class="dev">≠ Не за графіком: за планом світло є, але його немає</div>';return''}
function bars(L,cls){var h='',M=[];L.forEach(function(s){var l=M[M.length-1];if(l&&l[2]===s[2]&&l[3]===s[3]&&s[0]<=l[1])l[1]=Math.max(l[1],s[1]);else M.push(s.slice())});M.forEach(function(s){h+='<b class="'+s[2]+'" data-a="'+s[0]+'" data-z="'+s[1]+'" data-n="'+s[3]+'" style="left:'+(s[0]/14.4).toFixed(2)+'%;width:'+(Math.max((s[1]-s[0])/14.4,.3)+.2).toFixed(2)+'%"></b>'});return'<div class="tr'+(cls?' '+cls:'')+'">'+h+'</div>'}
function axis(){var h='<div class="ax"><span></span><div>';for(var q=0;q<=24;q+=6)h+='<em style="left:'+(q/24*100)+'%">'+p2(q)+':00</em>';return h+'</div></div>'}
function cDay(){var y=D.ys||{},W=S.d,h='<div class="hd"><h2>'+(W?'Завтра':'Сьогодні')+'</h2>'+(y.configured&&y.valid?'<div class="seg"><button data-d="0" class="'+(W?'':'a')+'">Сьогодні</button><button data-d="1" class="'+(W?'a':'')+'">Завтра</button></div>':'')+'</div>';
var date=W?y.tomorrowDate:y.todayDate,st=W?y.tomorrowStatus:y.todayStatus,plan=y.configured&&y.valid&&(!W||date)&&(W||todayOk()),em=st==='EmergencyShutdowns';
if(y.configured&&y.valid&&date)h+='<div class="mut">'+esc(fdIso(date))+(STX[st]||st?' · '+esc(STX[st]||st):'')+'</div>';
if(plan&&em)h+='<div class="ban"><b>🚨 Діють екстрені відключення.</b> Плановий графік зараз не застосовується.</div>';else if(plan&&st==='WaitingForSchedule')h+='<div class="ban a"><b>Графік очікує уточнення.</b> YASNO може змінити інтервали.</div>';
if(!y.configured)h+='<p class="mut">Адресу YASNO не налаштовано — показано лише фактичний стан.</p>';else if(!y.valid)h+='<p class="mut">Графік YASNO ще не завантажено.'+(y.error?' '+esc(y.error):'')+'</p>';
else if(W&&!date){h+='<p class="mut">YASNO ще не опублікував графік на завтра.</p>';return h}else if(!W&&!todayOk())h+='<p class="mut err">Графіка YASNO на сьогодні немає'+(date?' (останній — на '+esc(fdIso(date))+')':'')+'.</p>';
if(plan){var P=sl(W?y.tomorrow:y.today,0),L=[],t=0,base='bon',bl=em?'Світло за збереженим графіком (не діє)':'Світло за графіком';P.forEach(function(a){if(a.s>t)L.push([t,a.s,base,bl]);L.push([a.s,a.e,a.t==='Possible'?'bpos':'boff',a.t==='Possible'?'Можливе відключення':'Відключення за графіком']);t=Math.max(t,a.e)});if(t<1440)L.push([t,1440,base,bl]);h+='<div class="ln"><span>План</span>'+bars(L,em?'p dim':'p')+'</div>'}
var d0=today();if(!W&&D.sync&&d0){var F=clip(d0,d0+86400).map(function(s){return[(s[0]-d0)/60,(s[1]-d0)/60,s[2]?'bon':'boff',s[2]?'Світло було':'Світла не було']});h+='<div class="ln"><span>Факт</span><div class="tw">'+bars(F,'')+'<div class="now" style="left:'+((D.now-d0)/864).toFixed(2)+'%"></div></div></div>'}
h+=axis()+'<div class="tip" id="tip">'+(S.tip||'')+'</div><div class="lg"><span><i class="bon"></i>Світло є</span><span><i class="boff"></i>Відключення</span>'+'<span><i class="bnod"></i>Немає даних</span>'+(plan&&em?'<span>Блідий план — збережений графік, зараз не діє</span>':'')+'</div>';
if(plan){h+='<div class="slt'+(em?' dim':'')+'"><div class="l">'+(em?'За збереженим графіком (не діє)':'Відключення за графіком')+'</div>';if(!P.length)h+='<div class="mut" style="margin-top:4px">Відключень не заплановано</div>';else{h+='<div class="chips">';P.forEach(function(a){var q=a.t==='Possible';h+='<span class="chip'+(q?' a':'')+'">з '+mm(a.s)+' до '+mm(a.e)+'<small>'+dur((a.e-a.s)*60)+(q?' · можливе':'')+'</small></span>'});h+='</div>'}h+='</div>'}
if(y.error&&y.valid)h+='<p class="mut err">Остання помилка оновлення графіка: '+esc(y.error)+'</p>';return h}
var TABS=[['ov','Огляд'],['wk','Тиждень'],['vo','Напруга'],['hi','Історія'],['dv','🔒 Пристрій']];
function tabs(){var h='';TABS.forEach(function(t){if(t[0]==='dv'&&!D.dev)return;h+='<button data-k="'+t[0]+'" class="'+(S.tab===t[0]?'a':'')+'">'+t[1]+'</button>'});set('tabs',h)}
function wkRows(b){var S0=segs(),E=evs(),h='<div class="wk">';for(var i=0;i<7;i++){var d0=D.wk[b+i],d1=D.wk[b+i+1],L=(d1-d0)/1440,x='';
S0.forEach(function(s){var a=Math.max(s[0],d0),z=Math.min(s[1],d1,D.now);if(z<=a)return;x+='<b class="'+(s[2]?'bon':'boff')+'" data-a="'+a+'" data-z="'+z+'" data-n="'+(s[2]?'Світло було':'Світла не було')+'" data-w="1" style="left:'+((a-d0)/L/14.4).toFixed(2)+'%;width:'+(Math.max((z-a)/L/14.4,.3)+.2).toFixed(2)+'%"></b>'});
if(D.dev)E.forEach(function(e){if(e[0]>=d0&&e[0]<d1&&(e[1]==1||e[1]==2||e[1]==5))x+='<span class="mk'+(e[1]==5?' s':'')+'" style="left:'+((e[0]-d0)/L/14.4).toFixed(2)+'%"></span>'});
h+='<div class="ln"><span>'+esc(D.dl[b+i])+'</span><div class="tr">'+x+'</div></div>'}
h+='<div class="ax"><span></span><div>';for(var q=0;q<=24;q+=6)h+='<em style="left:'+(q/24*100)+'%">'+p2(q)+'</em>';return h+'</div></div></div>'}
function wkBlock(b){return'<div class="hd" style="margin-top:16px"><h2>'+esc(fdm(D.wk[b]+43200))+' – '+esc(fdm(D.wk[b+6]+43200))+'</h2><div class="seg"><button data-w="1" class="'+(S.w?'a':'')+'">◀ Попередній</button><button data-w="0" class="'+(S.w?'':'a')+'">Поточний</button></div></div>'+wkRows(b)
+'<div class="tip" id="wtip">'+(S.wt||'')+'</div><div class="lg"><span><i class="bon"></i>Світло є</span><span><i class="boff"></i>Світла немає</span><span><i class="bnod"></i>Немає даних</span>'+(D.dev?'<span><i style="background:var(--acc)"></i>Запуск пристрою</span><span><i style="background:#a78bfa"></i>Зупинка пристрою</span>':'')+'</div><p class="mut">Фактична історія за журналом пристрою.</p>'}
function pOv(){var s=D.st||{},h='<div class="c"><div class="g">';
if(D.sync){var d0=today();h+=kp('Зі світлом сьогодні',d0?dur(onIn(d0,D.now)):'—')+kp('Без світла сьогодні',d0?dur(offIn(d0,D.now)):'—')+kp('Відключень сьогодні',d0?cnt(d0):'—')}else h+=kp('Відключень усього',Math.max(0,(s.total||0)-(s.open?1:0)))+kp('Сумарно без світла',dur(s.offSum||0))+kp('Сумарно зі світлом',dur(s.onSum||0));
h+=kp('Найдовше відключення',s.exact?dur(s.maxOff):'—')+'</div>';
if(D.sync&&D.wk)h+=wkBlock(S.w?0:7);
if(D.evFull)h+='<p class="mut">«+» — журнал зберігає лише останні події; старіші вже витіснено.</p>';return h+'</div>'}
function pWk(){if(!D.sync||!D.wk)return'<div class="c mut">Час на пристрої ще не синхронізовано.</div>';var b=S.w?0:7,on=0,off=0,n=0;clip(D.wk[b],Math.min(D.wk[b+7],D.now)).forEach(function(s){if(s[2])on+=s[1]-s[0];else off+=s[1]-s[0]});evs().forEach(function(e){if(e[1]==3&&e[0]>=D.wk[b]&&e[0]<D.wk[b+7])n++});
return'<div class="c"><div class="g" style="grid-template-columns:repeat(3,1fr)">'+kp('Зі світлом',dur(on))+kp('Без світла',dur(off))+kp('Відключень',n)+'</div>'+wkBlock(b)+(D.wkimg?'<p><a href="week.png" target="_blank" rel="noopener">🖼 Картинка за 7 днів з графіком YASNO</a></p>':'')+'</div>'}
function pVo(){if(D.lite)return'<div class="c mut">Завантаження…</div>';var vs=vsamp(),lo=D.lim[0],hi=D.lim[1],h='<div class="c"><div class="hd"><h2>Напруга за 24 години</h2></div><p class="mut" style="margin-top:0">Межі '+lo+'–'+hi+' В. Shelly опитується кожні 5 хв, при відкритій сторінці — кожні ~30 с.</p>';VB=[];
if(!vs.length)h+='<p>Немає вимірювань Shelly за останні 24 години.</p>';else{var st=Math.floor(D.now/3600)*3600-82800,mn=1e9,mx=0,mnT=0,mxT=0;for(var i=0;i<24;i++)VB.push([]);
vs.forEach(function(v){var i=Math.floor((v[0]-st)/3600);if(i>=0&&i<24)VB[i].push(v);if(v[1]<mn){mn=v[1];mnT=v[0]}if(v[1]>mx){mx=v[1];mxT=v[0]}});
var y0=Math.min(lo-8,mn/10-3),y1=Math.max(hi+8,mx/10+3),X=34,PW=566,PH=150,bw=PW/24,Y=function(v){return 8+(y1-v)/(y1-y0)*PH},g='<svg viewBox="0 0 600 '+(PH+30)+'"><rect class="vz" x="'+X+'" y="'+Y(hi).toFixed(1)+'" width="'+PW+'" height="'+(Y(lo)-Y(hi)).toFixed(1)+'"/>';
[lo,hi].forEach(function(l){g+='<text x="'+(X-4)+'" y="'+(Y(l)+4).toFixed(1)+'" font-size="10" text-anchor="end">'+l+'</text>'});
VB.forEach(function(B,i){if(!B.length)return;var a=1e9,z=0;B.forEach(function(v){if(v[1]<a)a=v[1];if(v[1]>z)z=v[1]});var t=Y(z/10),u=Y(a/10);g+='<rect data-h="'+i+'" class="'+((a/10<lo||z/10>hi)?'vo':'vb')+'" x="'+(X+i*bw+2).toFixed(1)+'" y="'+t.toFixed(1)+'" width="'+(bw-4).toFixed(1)+'" height="'+Math.max(u-t,3).toFixed(1)+'" rx="2"/>'});
for(var q=0;q<=24;q+=6)g+='<text x="'+(X+q*bw).toFixed(1)+'" y="'+(PH+26)+'" font-size="10" text-anchor="'+(q==24?'end':'middle')+'">'+hm(st+q*3600)+'</text>';
h+=g+'</svg><div class="tip" id="vtip">'+(S.vt||'Натисніть на стовпчик, щоб побачити вимірювання за годину.')+'</div><div class="g" style="margin-top:10px">'+kp('Мінімум',(mn/10).toFixed(1)+' В <span class="l">'+hm(mnT)+'</span>',mn/10<lo?'off':'')+kp('Максимум',(mx/10).toFixed(1)+' В <span class="l">'+hm(mxT)+'</span>',mx/10>hi?'off':'')+kp('Зараз',D.pw&&D.volt!=null?D.volt.toFixed(1)+' В':'—')+kp('Вимірювань',vs.length)+'</div>'}
var sp=(D.sp||[]).slice().reverse();h+='<details id="spd"'+(S.sp?' open':'')+'><summary>Виходи за межі ('+sp.length+')</summary>';
if(!sp.length)h+='<p class="mut">Виходів напруги за межі не зафіксовано.</p>';else{h+='<div class="sc"><table>';sp.forEach(function(s){var v=s[1]/10,low=v<s[2];h+='<tr><td>'+esc(fdm(s[0]))+' '+hm(s[0])+'</td><td class="le">'+v.toFixed(1)+' В</td><td class="r">'+(low?'−':'+')+Math.abs(v-s[2]).toFixed(1)+' В '+(low?'нижче':'вище')+' '+s[2]+' В</td></tr>'});h+='</table></div>'}
return h+'</details></div>'}
function outs(){var o=[],c=null;evs().forEach(function(e){var k=e[1],t=e[0];if(k==3){if(c)o.push(c);c={a:t,b:0}}else if(k==4){if(c){c.b=t;o.push(c);c=null}}else if(k==1){if(c){c.u=1;o.push(c);c=null}}else if(k==2){if(!c)c={a:0,b:0}}});if(c){c.o=1;o.push(c)}return o}
function dayName(t){var k=kyDate(t);return k===kyDate(D.now)?'Сьогодні':k===kyDate(D.now-86400)?'Вчора':fdm(t)}
function pHi(){var s=D.st||{},R=[],h='<div class="c"><div class="g">'+(D.sync?kp('За 24 год',cnt(D.now-86400))+kp('За 30 днів',cnt(D.now-2592000)):'')+kp('Завершених',Math.max(0,(s.total||0)-(s.open?1:0)))+kp('Найдовше',s.exact?dur(s.maxOff):'—')+kp('Сумарно без світла',dur(s.offSum||0))+kp('Сумарно зі світлом',dur(s.onSum||0))+'</div>';
outs().forEach(function(x){R.push({t:x.a||x.b||D.now,x:x})});if(D.bl&&D.bl.on)(D.bl.l||[]).forEach(function(b){if(b[0])R.push({t:b[0],b:b[1]})});R.sort(function(a,b){return b.t-a.t});R=R.slice(0,60);
if(!R.length)h+='<p class="mut">Відключень ще не зафіксовано.</p>';var dn='';R.forEach(function(r){var d=dayName(r.t);if(d!==dn){h+=(dn?'</table>':'')+'<div class="day">'+esc(d)+'</div><table>';dn=d}
if(r.b!=null){h+='<tr><td class="mut">'+hm(r.t)+' · ⚡ коротке зникнення</td><td class="r">'+r.b+' с</td></tr>';return}var x=r.x;
if(x.o){h+='<tr><td class="le"><b>● '+(x.a?hm(x.a):'?')+' → триває</b></td><td class="r">'+(x.a?dur(D.now-x.a):'—')+'</td></tr>';return}
var e=x.b?((x.a&&kyDate(x.a)!==kyDate(x.b))?fdm(x.b)+' ':'')+hm(x.b):(x.u?'перезапуск пристрою':'невідомо');h+='<tr><td>'+(x.a?hm(x.a):'невідомо')+' → '+e+'</td><td class="r">'+((x.a&&x.b)?dur(x.b-x.a):'—')+'</td></tr>'});if(dn)h+='</table>';
if(D.bl)h+='<p class="mut">'+(D.bl.on?'⚡ — світло повернулося швидше ніж за '+D.bl.lim+' с: не відключення, у статистику не потрапляє. За 24 год: '+D.bl.d+', з запуску: '+D.bl.n+'.':'Розпізнавання коротких зникнень вимкнено: кожне зникнення — відключення.')+'</p>';return h+'</div>'}
function pDv(){var v=D.dev,s=SYS,h='<div class="c"><div class="g">'+kp('Wi-Fi',v.rssi+' dBm')+kp('Вільна пам\'ять',Math.round(v.heap/1024)+' КБ')+kp('Без перезапуску',dur(v.up))+kp('Telegram',v.tg?'на зв\'язку':'недоступний',v.tg?'on':'off')+'</div><table style="margin-top:10px">'
+row('Версія прошивки',esc(v.ver))+row('Локальна IP',esc(v.ip))+row('Wi-Fi мережа',esc(v.ssid))+row('Shelly IP',esc(v.shelly||'не задано'))+row('Груп і каналів',v.chats)+row('Публічна сторінка',v.link?'<a href="'+esc(v.link)+'" target="_blank" rel="noopener">'+esc(v.link)+'</a>':'не налаштовано');
if(s){var a=s.adc;h+=row('Запусків усього',s.boots)+row('Аварійних перезапусків',s.crash)+row('Причина останнього запуску',esc(s.reason)+(s.why?' ('+esc(s.why)+')':''))+row('Пам\'ять мін. / найбільший блок',Math.round(s.minHeap/1024)+' / '+Math.round(s.maxBlock/1024)+' КБ')
+row('Сховище налаштувань',s.nvsUsed+' з '+s.nvsTotal+' записів')+row('Процесор',s.cpu+' МГц')+row('Watchdog',s.wdt?'активний, 60 с':'вимкнений')+row('Безпечний режим',s.safe?'так':'ні')+row('Сертифікат Telegram',s.tls==2?'не проходить перевірку':'перевіряється')
+row('Датчик світла (за хвилину)',a.n?('мін. '+a.min+', макс. '+a.max+', сер. '+a.avg+', розкид '+a.sd+', сіра зона '+a.grey+'%'):'збирається')+row('Стан датчика',a.flags&7?'⚠️ '+((a.flags&1?'нестабільний ':'')+(a.flags&2?'біля порогів ':'')+(a.flags&4?'не змінюється':'')):'✅ норма')}
h+='</table><p style="margin:14px 0 0"><button class="btn s" id="lo">Вийти</button></p></div><div class="c"><div class="hd"><h2>Журнал подій</h2></div>';
if(!s)return h+'<p class="mut">Завантаження…</p></div>';h+='<div class="lgf">'+[['Усі',0],['Попередження',1],['Помилки',2]].map(function(b){return'<button data-f="'+b[1]+'" class="'+(S.lf===b[1]?'a':'')+'">'+b[0]+'</button>'}).join('')+'</div><div class="sc"><table>';
var L=(s.log||[]).filter(function(e){return(e.s||0)>=S.lf});if(!L.length)h+='<tr><td class="mut">Подій немає</td></tr>';
L.forEach(function(e){var c=e.s==2?'le':e.s==1?'lw':'';h+='<tr><td class="mut" style="width:110px">'+(e.t?esc(fdm(e.t))+' '+hm(e.t):(e.p?'попер. сеанс, ':'')+'+'+e.up+' с')+'</td><td class="'+c+'"><b style="font-weight:600">'+esc(e.c)+'</b> · '+esc(e.m)+'</td></tr>'});
return h+'</table></div><p class="mut">Зберігаються останні 60 подій; попередження й помилки переживають перезапуск.</p></div>'}
function pane(){if(!D)return;var t=S.tab;set('pane',t==='wk'?pWk():t==='vo'?pVo():t==='hi'?pHi():t==='dv'&&D.dev?pDv():pOv())}
function go(k){S.tab=k;try{history.replaceState(null,'','#'+k)}catch(e){}tabs();pane();if(k==='dv')loadSys();if(k==='vo'&&D.lite)load();window.scrollTo(0,Math.min(window.scrollY,$('tabs').offsetTop-10))}
function tipOf(b){var a=+b.getAttribute('data-a'),z=+b.getAttribute('data-z'),n=b.getAttribute('data-n');if(b.getAttribute('data-w'))return'<b>'+esc(fdm(a))+', '+hm(a)+' – '+hm(z)+'</b> · '+esc(n)+' · '+dur(z-a);return'<b>'+mm(a)+' – '+mm(z)+'</b> · '+esc(n)+' · '+dur((z-a)*60)}
function cl(t,q){return t.closest?t.closest(q):null}
document.addEventListener('click',function(ev){var t=ev.target,e;
if((e=cl(t,'#tabs button')))return go(e.getAttribute('data-k'));
if((e=cl(t,'#day .seg button'))){S.d=+e.getAttribute('data-d');S.tip='';set('day',cDay());return}
if((e=cl(t,'#day .tr b'))){S.tip=tipOf(e);$('tip').innerHTML=S.tip;return}
if((e=cl(t,'#pane .tr b'))){S.wt=tipOf(e);var w=$('wtip');if(w)w.innerHTML=S.wt;return}
if((e=cl(t,'#pane .seg button[data-w]'))){S.w=+e.getAttribute('data-w');S.wt='';pane();return}
if((e=cl(t,'#pane rect[data-h]'))){var B=VB[+e.getAttribute('data-h')]||[];S.vt=B.map(function(v){return hm(v[0])+' — <b>'+(v[1]/10).toFixed(1)+' В</b>'}).join(' · ');$('vtip').innerHTML=S.vt;return}
if((e=cl(t,'#pane [data-f]'))){S.lf=+e.getAttribute('data-f');pane();return}
if(cl(t,'#lo'))fetch('api/logout',{method:'POST',credentials:'same-origin'}).then(function(){login('')})});
document.addEventListener('toggle',function(e){if(e.target&&e.target.id==='spd'){S.sp=e.target.open?1:0;LAST.pane=null}},true);
setInterval(function(){if(D&&SK&&!document.hidden){hero();if(FAIL)stale()}},20000);
load();
</script></body></html>)lwpage";

static const uint8_t lw_favicon_png[] PROGMEM = {
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
  0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x08, 0x06, 0x00, 0x00, 0x00, 0xaa, 0x69, 0x71,
  0xde, 0x00, 0x00, 0x07, 0x72, 0x49, 0x44, 0x41, 0x54, 0x78, 0x01, 0xd4, 0x59, 0xcf, 0x8b, 0x1c,
  0x45, 0x14, 0x7e, 0x15, 0x3c, 0x78, 0xf0, 0xe0, 0x41, 0x44, 0x44, 0xc4, 0x83, 0xe8, 0xc5, 0x9f,
  0x28, 0x46, 0x51, 0xcc, 0x0c, 0x31, 0x88, 0x88, 0xe8, 0x25, 0x68, 0x0e, 0x22, 0x1e, 0x42, 0xc8,
  0x5f, 0xb1, 0x3d, 0x7f, 0x87, 0x07, 0x0f, 0x1e, 0x3c, 0x78, 0xf0, 0x28, 0x12, 0x64, 0x26, 0x44,
  0x8c, 0x18, 0x31, 0x98, 0x40, 0x02, 0x1e, 0x3c, 0x89, 0x88, 0x2c, 0x12, 0x25, 0x42, 0x84, 0xed,
  0x7a, 0x7e, 0xdf, 0xab, 0x7e, 0xbd, 0x35, 0xbb, 0xb3, 0xbb, 0xb3, 0x3b, 0x5d, 0x3d, 0x9d, 0xa5,
  0xbe, 0x79, 0xaf, 0x5e, 0xf5, 0x74, 0xd5, 0xfb, 0xbe, 0xfa, 0xd1, 0x3d, 0x7b, 0x4c, 0xee, 0x92,
  0x3f, 0xbd, 0xf9, 0xc6, 0xa8, 0xc4, 0x50, 0x07, 0x4f, 0x00, 0x12, 0xaf, 0xf4, 0xe6, 0x49, 0x15,
  0x89, 0x53, 0xd8, 0x69, 0xd7, 0x24, 0x0c, 0x9a, 0x00, 0x26, 0x2f, 0x1a, 0x37, 0x00, 0x31, 0xc4,
  0xba, 0xf3, 0x59, 0x30, 0x58, 0x02, 0x90, 0x3c, 0x92, 0xad, 0x37, 0x44, 0xa2, 0x88, 0x60, 0x02,
  0x28, 0x10, 0x74, 0x86, 0x4a, 0xa7, 0x65, 0xb0, 0x04, 0x48, 0xdc, 0x9a, 0x4a, 0x44, 0xf2, 0x86,
  0x1a, 0x1c, 0xc0, 0xaf, 0xeb, 0x8b, 0x9d, 0x66, 0x8f, 0x9b, 0x0d, 0x92, 0x00, 0xbd, 0x71, 0x02,
  0x6b, 0x1d, 0x09, 0x53, 0x79, 0x87, 0x46, 0x09, 0x4f, 0x7f, 0x5b, 0x61, 0xcc, 0x9d, 0x96, 0xc1,
  0x11, 0xa0, 0xd7, 0x5f, 0xaf, 0xa0, 0xfc, 0x08, 0x10, 0x03, 0x12, 0x37, 0x2b, 0x71, 0xd6, 0x69,
  0xe6, 0xcd, 0xcd, 0x06, 0x45, 0x80, 0x25, 0x1f, 0xb0, 0xe9, 0xb9, 0xea, 0x5c, 0xff, 0x24, 0x80,
  0xf5, 0xa8, 0x9d, 0x4f, 0x7f, 0x72, 0x30, 0x18, 0x02, 0xf4, 0xfa, 0x6b, 0x98, 0xde, 0xd8, 0xf4,
  0x7c, 0xcd, 0x47, 0xac, 0x7b, 0xfa, 0x24, 0x00, 0x7e, 0x78, 0xf6, 0x7b, 0xb4, 0x73, 0xc8, 0xdd,
  0x62, 0x10, 0x04, 0xa4, 0xe4, 0xa1, 0x3c, 0x77, 0x7a, 0xaa, 0x9d, 0x23, 0xc5, 0x8a, 0x4c, 0x7f,
  0x52, 0xb9, 0x76, 0x02, 0xf4, 0xfa, 0xcb, 0x23, 0xd1, 0x46, 0x79, 0xa5, 0xea, 0x44, 0x14, 0x3b,
  0xf7, 0x59, 0x37, 0x94, 0x99, 0xfe, 0x83, 0x20, 0x00, 0x62, 0x4f, 0x25, 0xa9, 0x8c, 0xf1, 0xe8,
  0x36, 0x18, 0x6b, 0x10, 0x9e, 0xff, 0xb1, 0xc8, 0xf4, 0x47, 0x67, 0xb2, 0xd6, 0x19, 0xa0, 0xd7,
  0x8e, 0x4f, 0x6d, 0x87, 0xa7, 0xca, 0x58, 0xe7, 0xc9, 0x87, 0xfa, 0xf4, 0x19, 0xe3, 0xfa, 0x2f,
  0xb4, 0xfb, 0x33, 0x79, 0x62, 0x6d, 0x04, 0xe8, 0xcf, 0x2f, 0x55, 0x12, 0x15, 0x4f, 0x7b, 0x8a,
  0x71, 0x64, 0xb0, 0xa4, 0x51, 0x6f, 0xd4, 0x17, 0x8d, 0x45, 0x76, 0x7f, 0x74, 0x6a, 0x65, 0x2d,
  0x04, 0xe8, 0x55, 0x26, 0xcf, 0x4d, 0x8f, 0xeb, 0x9d, 0x80, 0xea, 0x4c, 0xdc, 0x55, 0x37, 0x9f,
  0xb1, 0x5a, 0xc2, 0x0b, 0xd7, 0x8a, 0x4d, 0x7f, 0x32, 0xd0, 0x3b, 0x01, 0x7a, 0xf5, 0x45, 0x24,
  0x84, 0xe4, 0xb1, 0xf8, 0xb1, 0xd3, 0x61, 0x0c, 0x50, 0x9b, 0x7e, 0xab, 0x38, 0xea, 0xf4, 0x19,
  0x13, 0x9d, 0xe1, 0x82, 0x4e, 0xcb, 0xce, 0x9b, 0xf5, 0x4a, 0x80, 0x25, 0x6f, 0x6f, 0x77, 0xb5,
  0x08, 0xd7, 0x39, 0xcf, 0x79, 0x03, 0xea, 0x73, 0xea, 0xa3, 0xce, 0xf6, 0xba, 0xec, 0xf4, 0x27,
  0x19, 0xbd, 0x11, 0x80, 0xe4, 0xd3, 0x71, 0x67, 0x4f, 0x77, 0x8a, 0xbe, 0x09, 0x4c, 0x73, 0xd6,
  0xa9, 0x36, 0x55, 0x6f, 0xa7, 0x3e, 0xdb, 0x54, 0xc2, 0xf1, 0x1b, 0x98, 0x2d, 0xb8, 0xb4, 0x60,
  0xe9, 0x8d, 0x00, 0xe1, 0xdb, 0x1d, 0x13, 0xa4, 0xe2, 0xb4, 0x44, 0xee, 0xb3, 0x9e, 0x23, 0xc6,
  0xe2, 0xd3, 0x9f, 0xbc, 0xf6, 0x42, 0x80, 0xfe, 0xf4, 0x1c, 0xde, 0xee, 0x14, 0xfd, 0x39, 0xa2,
  0x08, 0x15, 0xcf, 0x95, 0xe7, 0x4c, 0xf0, 0x58, 0xf2, 0x8b, 0xee, 0xfe, 0x18, 0x8c, 0x95, 0xe2,
  0x04, 0xe8, 0x95, 0x67, 0x2a, 0xe1, 0x2f, 0x39, 0x5c, 0xd3, 0x54, 0xdc, 0xc1, 0x35, 0x4f, 0xdf,
  0x55, 0x77, 0x9f, 0x16, 0x08, 0xaf, 0xfc, 0x52, 0x7c, 0xfa, 0x93, 0x81, 0xa2, 0x04, 0x58, 0xf2,
  0x82, 0x1d, 0x3f, 0x57, 0x96, 0xea, 0xba, 0xf2, 0xf4, 0x8d, 0x00, 0xc5, 0x58, 0x00, 0xfa, 0x8c,
  0xf5, 0xb0, 0xfb, 0xa3, 0x43, 0x2b, 0xc5, 0x08, 0xd0, 0x2b, 0x4f, 0x41, 0x41, 0x3c, 0xe3, 0x33,
  0x29, 0x02, 0xaa, 0x0a, 0xe1, 0x3e, 0x6d, 0x8e, 0xbc, 0xad, 0xd0, 0xab, 0xaf, 0x65, 0xbc, 0xe3,
  0xa3, 0x08, 0x01, 0x29, 0x79, 0xdd, 0x68, 0xd7, 0xb9, 0xa9, 0x8a, 0x75, 0x7f, 0xb0, 0xf2, 0x82,
  0xcd, 0x41, 0xc2, 0xab, 0xbf, 0x82, 0x3c, 0xb8, 0x3d, 0x94, 0xce, 0x09, 0x40, 0xf2, 0xe9, 0xb8,
  0xe3, 0x9a, 0xa7, 0xc2, 0xb4, 0x54, 0xd7, 0x31, 0xb7, 0xf6, 0x9b, 0xf3, 0xde, 0xae, 0x03, 0x41,
  0x76, 0x6d, 0xdd, 0xcb, 0xee, 0xef, 0xdc, 0x76, 0x4e, 0x80, 0xd8, 0x71, 0x87, 0xf5, 0x6c, 0x6a,
  0x23, 0x29, 0xda, 0x16, 0xa8, 0xab, 0xb7, 0xc1, 0xe6, 0x7e, 0x7b, 0x8d, 0x8a, 0x5e, 0x7a, 0xa4,
  0xd2, 0x8b, 0xc4, 0xc3, 0xb0, 0x0d, 0xa6, 0xb0, 0x86, 0x87, 0x2a, 0xfd, 0x26, 0xc3, 0x05, 0xf8,
  0x17, 0x1e, 0xac, 0x94, 0xf8, 0x1a, 0x96, 0xf8, 0xea, 0x01, 0xbc, 0x63, 0x78, 0x8a, 0xfb, 0xdb,
  0x4e, 0x09, 0xd0, 0x1f, 0x9e, 0x9c, 0x0a, 0xd5, 0x24, 0xa8, 0xb8, 0x5b, 0xf7, 0x59, 0x37, 0xd4,
  0x98, 0xe9, 0x24, 0x03, 0xb0, 0x36, 0xd6, 0x01, 0xf3, 0xe3, 0x08, 0x7b, 0xc5, 0x86, 0x08, 0xf7,
  0x0f, 0xdd, 0x30, 0x3f, 0x72, 0x23, 0x25, 0x10, 0x8b, 0x88, 0x29, 0x81, 0xba, 0xc5, 0x11, 0x63,
  0x9d, 0x71, 0x69, 0xe2, 0x22, 0xfd, 0x13, 0xa0, 0x97, 0x9f, 0xa8, 0x24, 0x2a, 0x3a, 0x56, 0x91,
  0x7c, 0xcd, 0x9b, 0xb2, 0x51, 0x12, 0x31, 0x6c, 0x03, 0x14, 0xb0, 0x6b, 0x68, 0x1b, 0x58, 0x0c,
  0x3e, 0xad, 0x01, 0xb7, 0x41, 0xd5, 0xbe, 0x6e, 0x16, 0x1f, 0x28, 0x62, 0x6d, 0x70, 0x50, 0xb6,
  0xdb, 0x78, 0x2d, 0x02, 0xd6, 0x26, 0x12, 0xde, 0xda, 0xac, 0x64, 0xc9, 0xbf, 0x4e, 0x66, 0x80,
  0x25, 0x6f, 0xc7, 0x1d, 0x12, 0xb5, 0x75, 0x4c, 0x4b, 0x40, 0xd5, 0x83, 0xd7, 0xbc, 0x08, 0x95,
  0x8f, 0x48, 0x60, 0x0e, 0xfc, 0x7e, 0x8e, 0x1d, 0xed, 0xb5, 0xb7, 0x31, 0x4e, 0x9f, 0x16, 0xd8,
  0x92, 0xf1, 0x92, 0xb9, 0xdb, 0x65, 0x2b, 0x13, 0xa0, 0x97, 0x1f, 0x07, 0xdb, 0x71, 0x43, 0x28,
  0x07, 0xa7, 0x37, 0x6d, 0x0e, 0xaa, 0xc2, 0x3a, 0x2d, 0xe1, 0xbe, 0x59, 0x8c, 0x01, 0x63, 0xa6,
  0x3b, 0xaf, 0x2c, 0x82, 0x28, 0x29, 0xee, 0xd7, 0x20, 0xc0, 0xef, 0xc3, 0x58, 0x1c, 0x61, 0xb3,
  0x79, 0xac, 0x0e, 0xe3, 0xf0, 0xce, 0xe6, 0x8c, 0x4d, 0xcb, 0x62, 0x25, 0x02, 0x2c, 0x79, 0x7b,
  0xbb, 0xa3, 0x02, 0x00, 0x09, 0x30, 0x35, 0xa9, 0x7c, 0x53, 0x67, 0x8c, 0xb3, 0xc0, 0x80, 0x18,
  0xdb, 0x19, 0x8b, 0xc8, 0x64, 0x21, 0x70, 0x0d, 0x93, 0x5a, 0xd8, 0xc6, 0xef, 0xa0, 0x9d, 0xf7,
  0xd8, 0xdd, 0x3e, 0x39, 0x6c, 0xf2, 0x24, 0xe9, 0xc8, 0x04, 0x20, 0xf9, 0x91, 0x30, 0x79, 0xca,
  0xc0, 0x84, 0x76, 0xad, 0x69, 0x0c, 0x94, 0x89, 0xb4, 0x40, 0x77, 0x18, 0x3f, 0x2f, 0x4f, 0x40,
  0xa5, 0x6d, 0xa3, 0xbf, 0x57, 0x3b, 0xe3, 0x07, 0xb5, 0x0b, 0x92, 0xff, 0x0b, 0x33, 0x11, 0xd7,
  0xee, 0x28, 0x07, 0x55, 0x8f, 0x4c, 0x40, 0x3a, 0xee, 0xa0, 0x34, 0xd7, 0x3c, 0x09, 0x30, 0x55,
  0x9a, 0x3a, 0xfd, 0x36, 0x06, 0x22, 0x4c, 0x2d, 0xda, 0x1c, 0x48, 0xca, 0xe2, 0x6e, 0xb3, 0x36,
  0x5b, 0xdf, 0x79, 0x9c, 0xbe, 0xb7, 0xbb, 0x4f, 0x4b, 0xc4, 0x23, 0x27, 0x4f, 0x72, 0x8e, 0x44,
  0x80, 0x7e, 0xf7, 0xd8, 0x54, 0x5c, 0x3d, 0xca, 0x49, 0x9f, 0x36, 0x07, 0x63, 0x06, 0x74, 0xd3,
  0x5a, 0xfa, 0x0e, 0x0c, 0xbe, 0x8d, 0xd3, 0xf7, 0xb8, 0x5b, 0xc6, 0x08, 0xaf, 0xbb, 0xcd, 0x63,
  0x3a, 0x09, 0xef, 0xde, 0x3a, 0x92, 0xf2, 0xb8, 0x9b, 0x95, 0x43, 0x13, 0xa0, 0x97, 0x1e, 0xad,
  0x84, 0x6f, 0x77, 0x73, 0x6b, 0x9a, 0xca, 0xe7, 0x0a, 0xa9, 0x08, 0xd5, 0xb5, 0x04, 0x19, 0x6f,
  0xea, 0x8c, 0xed, 0xc2, 0x82, 0x76, 0x7e, 0xaf, 0xbd, 0x8e, 0xed, 0xc4, 0xce, 0x7b, 0xc4, 0xd9,
  0xaa, 0xc9, 0x93, 0x81, 0x43, 0x11, 0x60, 0xc9, 0xdb, 0xc3, 0x06, 0x06, 0x33, 0xa7, 0x36, 0x6e,
  0xc5, 0x41, 0x5b, 0x18, 0x1f, 0xad, 0xcf, 0xb8, 0xc3, 0xe3, 0x6e, 0x9b, 0x38, 0x4c, 0xba, 0x55,
  0x1e, 0xa7, 0x8f, 0x06, 0x98, 0xd4, 0x46, 0x1f, 0x15, 0xbf, 0x2f, 0xde, 0x16, 0xc3, 0x7b, 0x7f,
  0x1f, 0xea, 0xb8, 0xc3, 0x1d, 0x16, 0x96, 0xa5, 0x09, 0xe0, 0xe3, 0x69, 0x7a, 0x3a, 0x83, 0xda,
  0x73, 0xeb, 0x3b, 0x42, 0x6d, 0x02, 0x03, 0x6c, 0x55, 0xa3, 0x8f, 0x98, 0xad, 0x65, 0x58, 0xee,
  0x09, 0x73, 0x6d, 0x4d, 0x3b, 0xe3, 0xb5, 0xfb, 0xb4, 0x04, 0xaf, 0x77, 0x9b, 0xfb, 0x8c, 0x11,
  0x51, 0xba, 0x4a, 0x9e, 0x8c, 0x2c, 0x4d, 0x40, 0xda, 0xe4, 0x31, 0x00, 0x94, 0xb4, 0xfe, 0xf1,
  0x75, 0xf3, 0xdd, 0xa2, 0xe2, 0x0a, 0xb5, 0xd6, 0xdb, 0x68, 0xf3, 0x76, 0xd6, 0x1d, 0x8c, 0x2f,
  0xf2, 0x17, 0xc5, 0x70, 0x6d, 0x7d, 0xac, 0x13, 0xe5, 0x71, 0x77, 0x2b, 0x4b, 0x13, 0x10, 0x4e,
  0xfc, 0x56, 0x49, 0x2d, 0x13, 0x89, 0x11, 0xd0, 0x89, 0xd4, 0x0d, 0x22, 0x62, 0x16, 0x47, 0x3d,
  0x02, 0x16, 0x47, 0x8c, 0x71, 0x85, 0x65, 0xdd, 0xe2, 0xf4, 0x01, 0xaa, 0x6e, 0x50, 0x49, 0x44,
  0xc2, 0xda, 0xec, 0x88, 0x22, 0xad, 0x75, 0xdf, 0xdb, 0x68, 0x11, 0xab, 0xe3, 0x38, 0x9c, 0xbe,
  0x35, 0xb3, 0x91, 0x77, 0xf4, 0xb1, 0x34, 0x01, 0xec, 0x2f, 0x8c, 0x7f, 0xaf, 0xc2, 0xf8, 0x8f,
  0x84, 0x93, 0xb0, 0x8e, 0x53, 0xf0, 0x4f, 0xfd, 0x59, 0x05, 0xe2, 0x4d, 0xd8, 0x1c, 0x78, 0x2e,
  0xe7, 0xb3, 0x79, 0x78, 0x7b, 0x33, 0xed, 0xd6, 0xc8, 0x25, 0xad, 0x6b, 0x38, 0xed, 0x4c, 0xa1,
  0x8f, 0x1e, 0x60, 0x52, 0x1b, 0x7d, 0x54, 0xd8, 0x0e, 0x37, 0xc5, 0xc2, 0x24, 0x9c, 0xbe, 0xdd,
  0x69, 0xf2, 0xbc, 0xf5, 0xa1, 0x08, 0xe0, 0x17, 0x56, 0x82, 0x2b, 0xef, 0x96, 0xeb, 0xdf, 0xf6,
  0x09, 0x24, 0x6b, 0x31, 0xa8, 0x6c, 0xb3, 0x80, 0xf5, 0x06, 0x6c, 0xaf, 0x23, 0x92, 0xff, 0x27,
  0x11, 0xb8, 0xd2, 0x00, 0x76, 0x7f, 0xb9, 0x5f, 0x02, 0xf8, 0xda, 0x4a, 0x55, 0x91, 0x5b, 0x52,
  0x15, 0x0e, 0x4a, 0xf2, 0x31, 0x38, 0xf3, 0xf1, 0xd1, 0x5e, 0x43, 0x5f, 0x26, 0xe1, 0xfd, 0xdb,
  0x45, 0x92, 0x47, 0x8f, 0xfd, 0xfd, 0x77, 0x58, 0xbf, 0xbc, 0x7f, 0x94, 0xd6, 0x38, 0x92, 0x32,
  0x95, 0xa1, 0xb6, 0x25, 0x0a, 0x6b, 0xea, 0x7b, 0xdc, 0x2d, 0xe2, 0xd8, 0x5b, 0x4a, 0x26, 0xdf,
  0x2b, 0x01, 0xe8, 0x6c, 0x94, 0x36, 0x3d, 0x78, 0x96, 0x38, 0x6d, 0x0e, 0x24, 0x3e, 0x1f, 0x9f,
  0x85, 0x33, 0xe5, 0x94, 0x47, 0xcf, 0x56, 0xfa, 0x5b, 0x02, 0xfc, 0xf5, 0xc6, 0x95, 0x6f, 0x2d,
  0x54, 0x36, 0x1f, 0xc9, 0xb7, 0xd6, 0x62, 0xb3, 0xf0, 0xc1, 0xbf, 0x9d, 0x1e, 0x77, 0x96, 0xed,
  0x82, 0x8f, 0x5e, 0x08, 0xd0, 0x2f, 0x30, 0xfd, 0x91, 0xe3, 0xf6, 0x0c, 0xc0, 0x48, 0xac, 0x4e,
  0x0b, 0x67, 0x5e, 0x79, 0x09, 0x67, 0xfa, 0x49, 0x1e, 0xbd, 0xf7, 0xb5, 0x07, 0x44, 0x4c, 0xff,
  0xd8, 0x9c, 0xf3, 0x48, 0x78, 0xaf, 0x35, 0xcf, 0x38, 0xce, 0x7a, 0x0e, 0xac, 0x2f, 0xf4, 0x32,
  0x03, 0x44, 0xf4, 0xc4, 0xf6, 0x4e, 0x0f, 0x02, 0x50, 0x52, 0x1d, 0x0e, 0x4a, 0xf2, 0x91, 0x72,
  0xd4, 0x71, 0xf8, 0xf0, 0xce, 0x4a, 0x67, 0x3d, 0xee, 0x72, 0xa8, 0xd2, 0x0f, 0x01, 0x75, 0x1c,
  0x09, 0xcf, 0x73, 0x2a, 0x3c, 0xbf, 0xd6, 0x45, 0x2c, 0x16, 0xf1, 0x9a, 0xa1, 0x93, 0xbe, 0x93,
  0x27, 0x53, 0xc5, 0x09, 0xd0, 0xcf, 0xef, 0xab, 0x5a, 0x85, 0x15, 0x5d, 0x2a, 0x3e, 0x0c, 0xf4,
  0x1b, 0xe0, 0xb1, 0x39, 0x7c, 0x74, 0xa7, 0xd8, 0x59, 0x8f, 0x5e, 0xf6, 0x2c, 0xc5, 0x09, 0x68,
  0x15, 0x66, 0xd2, 0xad, 0xfa, 0x20, 0xc1, 0x95, 0x5f, 0x63, 0xf2, 0x64, 0xa5, 0x3c, 0x01, 0xf6,
  0xfb, 0x01, 0xba, 0x42, 0xce, 0xbb, 0x4f, 0x01, 0x3c, 0xdf, 0xaf, 0x49, 0x79, 0x8c, 0xc8, 0x4a,
  0x51, 0x02, 0xf4, 0xb3, 0x7b, 0x2b, 0x69, 0x55, 0xc7, 0x3a, 0x37, 0x9f, 0x96, 0x90, 0xd9, 0xba,
  0xa6, 0xbd, 0x64, 0x7f, 0x45, 0x09, 0xb0, 0x7e, 0x38, 0xf5, 0x0d, 0xa8, 0xa9, 0x23, 0xcc, 0xc2,
  0xc7, 0xff, 0xf5, 0xf2, 0xa0, 0x83, 0x1e, 0xf7, 0x2d, 0x65, 0x09, 0xe0, 0xff, 0xeb, 0xa8, 0x3a,
  0x09, 0x68, 0xd7, 0xbc, 0xca, 0x50, 0x92, 0x27, 0x33, 0xc5, 0x08, 0xd0, 0x4f, 0x31, 0xfd, 0x99,
  0xb8, 0xa2, 0x9b, 0x1c, 0x38, 0xeb, 0x11, 0x19, 0x4c, 0x29, 0x46, 0x80, 0x98, 0xe2, 0xc8, 0x9c,
  0xd6, 0x9e, 0x01, 0xe8, 0xeb, 0x38, 0x9c, 0xdd, 0xea, 0xf5, 0x41, 0xe7, 0x20, 0xa6, 0xcb, 0x11,
  0x30, 0xb7, 0xfb, 0x63, 0x18, 0xf8, 0x79, 0x6c, 0x68, 0xc9, 0x63, 0x54, 0x65, 0xde, 0x05, 0xf4,
  0x93, 0x7b, 0x46, 0x69, 0x06, 0x70, 0xb7, 0xa7, 0xf2, 0xf8, 0x51, 0xe3, 0xec, 0xd6, 0x5a, 0x1e,
  0x74, 0x98, 0xe4, 0x7e, 0x28, 0x35, 0x03, 0x46, 0xdb, 0x4f, 0x7f, 0x3a, 0x09, 0xe7, 0x86, 0x99,
  0x3c, 0x89, 0x29, 0x43, 0x80, 0xbf, 0xfb, 0xe3, 0x17, 0xe1, 0x70, 0x2e, 0x0e, 0x52, 0x79, 0x26,
  0x4f, 0x94, 0x21, 0x00, 0xb3, 0x1e, 0x4f, 0x7d, 0xb3, 0x70, 0x7e, 0xd8, 0xc9, 0x17, 0x24, 0x20,
  0x8e, 0xc3, 0x79, 0x1d, 0xc4, 0x83, 0x0e, 0x93, 0xdc, 0x0f, 0x45, 0x66, 0x40, 0x38, 0x2f, 0xbd,
  0x1d, 0x75, 0xb2, 0xe2, 0xdf, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0x52, 0x74, 0x67, 0x4d, 0x00,
  0x00, 0x00, 0x06, 0x49, 0x44, 0x41, 0x54, 0x03, 0x00, 0xbc, 0x1b, 0xe8, 0xdb, 0x76, 0x13, 0xa4,
  0x65, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};

String fetchPublicIp() {
  if (WiFi.status() != WL_CONNECTED) {
    return String();
  }
  const char* urls[2] = { "http://api.ipify.org/", "http://checkip.amazonaws.com/" };
  const char* hosts[2] = { "api.ipify.org", "checkip.amazonaws.com" };
  for (int i = 0; i < 2; i++) {
    wdtFeed();
    if (!netDnsQuick(hosts[i], NET_DNS_WAIT_MS, NULL)) {
      continue;
    }
    WiFiClient c;
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(3000);
    http.setReuse(false);
    if (!http.begin(c, urls[i])) {
      continue;
    }
    int code = http.GET();
    String body;
    httpGetBodyBounded(http, code, 64, "Public IP service", body);
    http.end();
    body.trim();
    IPAddress ip;
    if (body.length() >= 7 && body.length() <= 15 && ip.fromString(body)) {
      return body;
    }
  }
  return String();
}

bool publicIpRefresh() {
  pubIpLastTry = millis();
  pubIpTried = true;
  String ip = fetchPublicIp();
  pubIpOk = ip.length() > 0;
  if (ip.length() == 0) {
    return false;
  }
  {
    if (pubIpAuto.length() > 0 && ip != pubIpAuto) {
      Serial.println("Публічна IP змінилася: " + pubIpAuto + " -> " + ip);

      logEvent(LC_WEB, LS_WARN, "Нова публічна IP " + ip + " - оновіть запис A");
      if (webHttpsState == 1) {
        webHttpsState = 0;
      }
    }
    pubIpAuto = ip;
    pubIpAt = statsNowEpoch();
  }
  return true;
}

void publicIpTick() {
  static bool wasOffline = false;
  static unsigned long backMs = 0;
  if (WiFi.status() != WL_CONNECTED || !telegramOnline) {
    wasOffline = true;
    return;
  }
  if (wasOffline) {
    wasOffline = false;
    backMs = millis();
    if (backMs == 0) backMs = 1;
  }
  unsigned long interval = pubIpOk ? PUBIP_REFRESH_MS : PUBIP_RETRY_MS;
  bool afterOutage = backMs != 0 && pubIpTried && millis() - backMs >= 60000UL;
  if ((!pubIpTried && millis() > 90000UL) || (pubIpTried && millis() - pubIpLastTry >= interval) || afterOutage) {
    backMs = 0;
    publicIpRefresh();
  }
}

String webPublicHost() {
  if (webDomOn) {
    return webDomain;
  }
  return webIpManual ? webIpVal : pubIpAuto;
}

String webPublicLink() {
  String h = webPublicHost();
  if (h.length() == 0) {
    return "";
  }
  if (webDomOn) {
    return "https://" + h + "/";
  }
  return "http://" + h + ":" + String(WEB_PUBLIC_PORT) + "/";
}

bool webValidDomain(const String& d) {
  if (d.length() < 4 || d.length() > 100 || d.indexOf('.') <= 0 || d.endsWith(".") || d.indexOf("..") >= 0) {
    return false;
  }
  for (unsigned int i = 0; i < d.length(); i++) {
    char c = d[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) {
      return false;
    }
  }
  return true;
}

bool webIsPrivateIp(const IPAddress& ip) {
  return ip[0] == 10 || ip[0] == 127 || ip[0] == 0 || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) ||
         (ip[0] == 192 && ip[1] == 168) || (ip[0] == 169 && ip[1] == 254) || (ip[0] == 100 && ip[1] >= 64 && ip[1] <= 127) || ip[0] >= 224;
}

String webCleanHost(String s) {
  s.trim();
  s.toLowerCase();
  if (s.startsWith("http://")) s = s.substring(7);
  if (s.startsWith("https://")) s = s.substring(8);
  int p = s.indexOf('/');
  if (p >= 0) s = s.substring(0, p);
  p = s.indexOf(':');
  if (p >= 0) s = s.substring(0, p);
  return s;
}

bool pbkdf2Sha256(const String& pass, const uint8_t* salt, size_t saltLen, uint32_t iter, uint8_t* out) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  bool ok = info != NULL && mbedtls_md_setup(&ctx, info, 1) == 0;
  const uint8_t* key = (const uint8_t*)pass.c_str();
  size_t keyLen = pass.length();
  uint8_t u[32];
  uint8_t blk[4] = { 0, 0, 0, 1 };
  ok = ok && mbedtls_md_hmac_starts(&ctx, key, keyLen) == 0;
  ok = ok && mbedtls_md_hmac_update(&ctx, salt, saltLen) == 0;
  ok = ok && mbedtls_md_hmac_update(&ctx, blk, 4) == 0;
  ok = ok && mbedtls_md_hmac_finish(&ctx, u) == 0;
  if (ok) {
    memcpy(out, u, 32);
  }
  for (uint32_t i = 1; ok && i < iter; i++) {
    ok = mbedtls_md_hmac_reset(&ctx) == 0 && mbedtls_md_hmac_update(&ctx, u, 32) == 0 && mbedtls_md_hmac_finish(&ctx, u) == 0;
    for (int b = 0; ok && b < 32; b++) {
      out[b] ^= u[b];
    }
  }
  mbedtls_md_free(&ctx);
  memset(u, 0, sizeof(u));
  if (!ok) {
    memset(out, 0, 32);
    logEvent(LC_WEB, LS_ERR, "Адмінпанель: помилка хешу пароля (mbedTLS)");
  }
  return ok;
}

void admLoad() {
  admUser = preferences.getString("admUser", "");
  admHasPass = false;
  uint8_t rec[52];
  if (admUser.length() > 0 && nvsGetExact("admH2", rec, sizeof(rec))) {
    uint32_t it;
    memcpy(&it, rec, 4);
    if (it >= ADM_ITER && it <= 200000UL) {
      admIter = it;
      memcpy(admSalt, rec + 4, 16);
      memcpy(admHash, rec + 20, 32);
      admHasPass = true;
    }
  }
}

bool admStore(const uint8_t* salt, const uint8_t* hash, uint32_t iter) {
  uint8_t rec[52];
  memcpy(rec, &iter, 4);
  memcpy(rec + 4, salt, 16);
  memcpy(rec + 20, hash, 32);
  return nvsPutVerified("admH2", rec, sizeof(rec));
}

void webSessionsClear() {
  for (int i = 0; i < WEB_SESS_MAX; i++) {
    webSess[i] = "";
  }
}

bool admSetPassword(const String& pass) {
  uint8_t salt[16];
  uint8_t hash[32];
  for (int i = 0; i < 16; i += 4) {
    uint32_t r = esp_random();
    memcpy(salt + i, &r, 4);
  }
  wdtFeed();
  if (!pbkdf2Sha256(pass, salt, 16, ADM_ITER, hash) || !admStore(salt, hash, ADM_ITER)) {
    return false;
  }
  memcpy(admSalt, salt, 16);
  memcpy(admHash, hash, 32);
  admIter = ADM_ITER;
  admHasPass = admUser.length() > 0;
  webSessionsClear();
  return true;
}

bool admSetLogin(const String& user) {
  preferences.putString("admUser", user);
  if (preferences.getString("admUser", "") != user) {
    return false;
  }
  admUser = user;
  webSessionsClear();
  return true;
}

bool admCheck(const String& user, const String& pass) {
  if (!admHasPass || pass.length() == 0 || pass.length() > 256) {
    return false;
  }
  uint8_t h[32];
  if (!pbkdf2Sha256(pass, admSalt, 16, admIter, h)) {
    return false;
  }
  uint8_t diff = (user == admUser) ? 0 : 1;
  for (int i = 0; i < 32; i++) {
    diff |= h[i] ^ admHash[i];
  }
  return diff == 0;
}

bool admValidLogin(const String& s) {
  if (s.length() < 3 || s.length() > 32) {
    return false;
  }
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (!(isAlphaNumeric(c) || c == '.' || c == '_' || c == '-') || (uint8_t)c > 127) {
      return false;
    }
  }
  return true;
}

static bool webFailLocked(const WebFailEntry& e) {
  return e.lockDur != 0 && millis() - e.lockStart < e.lockDur;
}

WebFailEntry& webFailEntry(uint32_t ip) {
  int slot = -1, oldest = -1;
  for (int i = 0; i < WEB_FAIL_SLOTS; i++) {
    if (webFails[i].ip == ip && (webFails[i].fails || webFails[i].lockDur)) return webFails[i];
    if (slot < 0 && webFails[i].fails == 0 && webFails[i].lockDur == 0) slot = i;
    if (!webFailLocked(webFails[i]) &&
        (oldest < 0 || millis() - webFails[i].lastMs > millis() - webFails[oldest].lastMs)) oldest = i;
  }
  if (slot < 0) slot = oldest;
  static WebFailEntry overflow;
  if (slot < 0) { overflow = WebFailEntry{ ip, 0, 0, 0, millis() }; return overflow; }
  webFails[slot] = WebFailEntry{ ip, 0, 0, 0, millis() };
  return webFails[slot];
}

unsigned long webLockLeftSec(uint32_t ip) {
  bool known = false;
  unsigned long minLeft = 0;
  bool allLocked = true;
  for (int i = 0; i < WEB_FAIL_SLOTS; i++) {
    WebFailEntry& e = webFails[i];
    const bool locked = webFailLocked(e);
    const unsigned long leftMs = locked ? e.lockDur - (millis() - e.lockStart) : 0;
    if (!locked) allLocked = false;
    else if (minLeft == 0 || leftMs < minLeft) minLeft = leftMs;
    if (e.ip != ip || (e.fails == 0 && e.lockDur == 0)) continue;
    known = true;
    if (locked) return leftMs / 1000 + 1;
  }
  if (!known && allLocked) return minLeft / 1000 + 1;
  return 0;
}

void webLoginFailed(uint32_t ip) {
  WebFailEntry& e = webFailEntry(ip);
  if (e.fails < 255) e.fails++;
  e.lastMs = millis();
  if (e.fails >= 5) {
    int extra = e.fails - 5;
    e.lockDur = 60000UL << (extra > 4 ? 4 : extra);
    e.lockStart = millis();
  }
}

void webLoginSucceeded(uint32_t ip) {
  for (int i = 0; i < WEB_FAIL_SLOTS; i++) {
    if (webFails[i].ip == ip) webFails[i] = WebFailEntry{ 0, 0, 0, 0, 0 };
  }
}

bool webCrossOrigin() {
  if (!server.hasHeader("Origin")) return false;
  String o = server.header("Origin");
  int p = o.indexOf("://");
  String host = p >= 0 ? o.substring(p + 3) : o;
  return host != server.hostHeader();
}

String webCookieToken() {
  if (!server.hasHeader("Cookie")) {
    return "";
  }
  String c = server.header("Cookie");
  int p = -1;
  for (int from = 0; (from = c.indexOf("lwsid=", from)) >= 0; from++) {
    if (from == 0 || c[from - 1] == ' ' || c[from - 1] == ';') { p = from; break; }
  }
  if (p < 0) {
    return "";
  }
  int e = c.indexOf(';', p);
  return c.substring(p + 6, e < 0 ? c.length() : e);
}

bool webAuthed() {
  String tok = webCookieToken();
  if (tok.length() != 32) {
    return false;
  }
  for (int i = 0; i < WEB_SESS_MAX; i++) {
    if (webSess[i].length() != 32) continue;
    uint8_t diff = 0;
    for (int k = 0; k < 32; k++) diff |= (uint8_t)(webSess[i][k] ^ tok[k]);
    if (diff == 0) {
      if (millis() - webSessMs[i] >= WEB_SESS_IDLE_MS) {
        webSess[i] = "";
        return false;
      }
      webSessMs[i] = millis();
      return true;
    }
  }
  return false;
}

bool webAdminLocal() {

  if (server.hasHeader("CF-Connecting-IP") || server.hasHeader("X-Forwarded-For")) {
    return false;
  }
  IPAddress ip = server.client().remoteIP();
  return ip[0] == 10 || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) || (ip[0] == 192 && ip[1] == 168) || (ip[0] == 169 && ip[1] == 254);
}

bool webAdminDenyRemote() {
  if (webAdminLocal()) {
    return false;
  }
  server.send(403, "text/plain; charset=utf-8", "Адмінпанель доступна лише з локальної мережі.");
  return true;
}

void webSecurityHeaders(WebServer& s) {
  s.sendHeader("X-Content-Type-Options", "nosniff");
  s.sendHeader("X-Frame-Options", "DENY");
  s.sendHeader("Referrer-Policy", "no-referrer");
  s.sendHeader("Cache-Control", "no-store");
}

void webAppendWeek(String& j, uint32_t now) {
  static const char* dn[7] = { "Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Нд" };
  time_t t = (time_t)now;
  struct tm tmNow;
  localtime_r(&t, &tmNow);
  int wd = (tmNow.tm_wday + 6) % 7;
  String wk = ",\"wk\":[";
  String dl = ",\"dl\":[";
  for (int i = 0; i < 15; i++) {
    struct tm d;
    memset(&d, 0, sizeof(d));
    d.tm_year = tmNow.tm_year;
    d.tm_mon = tmNow.tm_mon;
    d.tm_mday = tmNow.tm_mday - wd - 7 + i;
    d.tm_hour = 12;
    d.tm_isdst = -1;
    time_t noon = mktime(&d);
    struct tm dn0;
    localtime_r(&noon, &dn0);

    time_t e = noon - 12 * 3600;
    for (int k = 0; k < 3; k++) {
      struct tm x;
      localtime_r(&e, &x);
      if (x.tm_hour == 0 && x.tm_min == 0 && x.tm_sec == 0) {
        break;
      }
      if (x.tm_hour >= 12) {
        e += (24 - x.tm_hour) * 3600 - x.tm_min * 60 - x.tm_sec;
      } else {
        e -= x.tm_hour * 3600 + x.tm_min * 60 + x.tm_sec;
      }
    }
    wk += (i ? "," : "") + String((uint32_t)e);
    if (i < 14) {
      dl += String(i ? "," : "") + "\"" + dn[i % 7] + "\"";
    }
  }
  j += wk + "]" + dl + "]";
}

String webDataJson(bool admin, bool full) {
  String j;
  j.reserve(10000);
  uint32_t now = statsNowEpoch();
  bool on = !messageFlag;
  j += "{\"now\":" + String(now) + ",\"sync\":" + String(now ? 1 : 0) + ",\"pw\":" + String(on ? 1 : 0);
  j += ",\"since\":\"" + jsonEsc(on ? powerOnShortTime : powerOffShortTime) + "\"";
  j += ",\"dur\":\"" + jsonEsc(on ? currentOnDurationText() : currentOffDurationText()) + "\"";
  j += ",\"prev\":\"" + jsonEsc(on ? lastOffDurationText : lastOnDurationText) + "\"";
  int vi = (voltLog.head + VOLT_SIZE - 1) % VOLT_SIZE;
  const unsigned long vmAgeSec = vmLastVMs ? (millis() - vmLastVMs) / 1000UL : 0xFFFFFFFFUL;
  if (on && now != 0 && !isnan(vmLastV) && vmAgeSec <= 900UL && now > vmAgeSec) {
    j += ",\"volt\":" + String(vmLastV, 1) + ",\"voltT\":" + String(now - vmAgeSec);
  } else if (on && voltLog.count > 0 && now != 0 && now >= voltLog.t[vi] && now - voltLog.t[vi] <= 900) {
    j += ",\"volt\":" + String(voltLog.dv[vi] / 10.0f, 1) + ",\"voltT\":" + String(voltLog.t[vi]);
  } else {
    j += ",\"volt\":null";
  }
  uint32_t onSum = stats.totalOnSec + (statOnObserving ? (millis() - statOnSegMs) / 1000 : 0);
  uint32_t offSum = stats.totalOffSec;

  if (stats.offOpen) {
    uint32_t activeOff = 0;
    if (statOffInSession) activeOff = (millis() - statOffStartMs) / 1000UL;
    else if (stats.curOffStartEpoch != 0 && now >= stats.curOffStartEpoch) activeOff = now - stats.curOffStartEpoch;
    if (activeOff > 0) offSum += activeOff;
  }
  uint32_t displayedOffCount = stats.offCount;
  if (powerOutageCount > 0 && displayedOffCount < (uint32_t)powerOutageCount) displayedOffCount = (uint32_t)powerOutageCount;
  j += ",\"st\":{\"total\":" + String(displayedOffCount) + ",\"exact\":" + String(stats.exactCount) + ",\"maxOff\":" + String(stats.maxOffSec)
       + ",\"offSum\":" + String(offSum) + ",\"onSum\":" + String(onSum) + ",\"open\":" + String(stats.offOpen ? 1 : 0) + "}";
  j += ",\"lim\":[" + String(voltLow) + "," + String(voltHigh) + "]";
  if (now != 0) {
    webAppendWeek(j, now);
  }
  j += ",\"ev\":[";
  for (int i = 0; i < evLog.count; i++) {
    int idx = (evLog.head + EV_SIZE - evLog.count + i) % EV_SIZE;
    j += String(i ? "," : "") + "[" + String(evLog.t[idx]) + "," + String(evLog.k[idx]) + "]";
  }
  j += "],\"evFull\":" + String(evLog.count >= EV_SIZE ? 1 : 0);
  if (!full) {
    j += ",\"lite\":1";
  } else {
    j += ",\"vs\":[";
    for (int i = 0; i < voltLog.count; i++) {
      int idx = (voltLog.head + VOLT_SIZE - voltLog.count + i) % VOLT_SIZE;
      j += String(i ? "," : "") + "[" + String(voltLog.t[idx]) + "," + String(voltLog.dv[idx]) + "]";
    }
    j += "],\"sp\":[";
    for (int i = 0; i < spkLog.count; i++) {
      int idx = (spkLog.head + SPK_SIZE - spkLog.count + i) % SPK_SIZE;
      j += String(i ? "," : "") + "[" + String(spkLog.t[idx]) + "," + String(spkLog.dv[idx]) + "," + String(spkLog.lim[idx]) + "]";
    }
    j += "]";
  }

  j += ",\"ys\":{\"configured\":" + String(yasnoGroup.length() ? "true" : "false");
  j += ",\"valid\":" + String(yasnoScheduleValid ? "true" : "false");
  j += ",\"city\":\"" + jsonEsc(yasnoCity) + "\",\"group\":\"" + jsonEsc(yasnoGroup) + "\"";
  j += ",\"street\":\"" + jsonEsc(yasnoStreet) + "\",\"house\":\"" + jsonEsc(yasnoHouse) + "\"";
  j += ",\"today\":" + (yasnoScheduleValid ? yasnoTodaySlotsJson : String("[]"));
  j += ",\"tomorrow\":" + (yasnoScheduleValid ? yasnoTomorrowSlotsJson : String("[]"));
  j += ",\"todayDate\":\"" + jsonEsc(yasnoTodayDate) + "\",\"tomorrowDate\":\"" + jsonEsc(yasnoTomorrowDate) + "\"";
  j += ",\"todayStatus\":\"" + jsonEsc(yasnoTodayStatus) + "\",\"tomorrowStatus\":\"" + jsonEsc(yasnoTomorrowStatus) + "\"";
  j += ",\"updatedOn\":\"" + jsonEsc(yasnoScheduleUpdatedOn) + "\",\"fetchedAt\":" + String(yasnoScheduleFetchedAt);
  j += ",\"error\":\"" + (admin ? jsonEsc(yasnoTaskErrorCopy) : String(yasnoTaskErrorCopy.length() ? "Не вдалося оновити графік" : "")) + "\"}";
  if (admin) {
    j += ",\"bl\":" + blipJson();
    j += ",\"dev\":{\"ver\":\"V" LW_VERSION "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"ssid\":\"" + jsonEsc(WiFi.SSID()) + "\",\"rssi\":" + String(WiFi.RSSI())
         + ",\"up\":" + String(uptimeSec()) + ",\"heap\":" + String(ESP.getFreeHeap()) + ",\"shelly\":\"" + jsonEsc(shellyIP) + "\""
         + ",\"tg\":" + String(telegramOnline ? 1 : 0) + ",\"chats\":" + String(chatActiveCount()) + ",\"link\":\"" + jsonEsc(webPublicLink()) + "\"}";
  }
  j += "}";
  return j;
}

static void webSendPage(WebServer& s) {
  s.sendHeader("X-Content-Type-Options", "nosniff");
  s.sendHeader("X-Frame-Options", "DENY");
  s.sendHeader("Referrer-Policy", "no-referrer");
  s.sendHeader("Cache-Control", "no-cache");
  s.sendHeader("ETag", WEB_PAGE_ETAG);
  if (s.hasHeader("If-None-Match") && s.header("If-None-Match").indexOf(WEB_PAGE_ETAG) >= 0) {
    s.send(304);
    return;
  }
  s.send_P(200, "text/html; charset=utf-8", lw_page_html);
}

static void webSendIcon(WebServer& s) {
  s.sendHeader("Cache-Control", "public, max-age=" WEB_ICON_MAX_AGE);
  s.send_P(200, "image/png", (const char*)lw_favicon_png, sizeof(lw_favicon_png));
}

static void webAdminIcon() {
  webSendIcon(server);
}

static void webPubIcon() {
  webSendIcon(pubServer);
}

void webAdminPage() {
  if (!webAdminLocal()) {
    webSecurityHeaders(server);
    webAdminDenyRemote();
    return;
  }
  webSendPage(server);
}

void webAdminVoltageRefresh() {
  webSecurityHeaders(server);
  if (webAdminDenyRemote()) return;
  if (!admHasPass) { server.send(403, "application/json", "{\"nopass\":1}"); return; }
  if (!webAuthed()) { server.send(401, "application/json", "{\"auth\":0}"); return; }
  bool updated = webVoltageRefreshNow();
  server.send(200, "application/json", String("{\"ok\":1,\"updated\":") + (updated ? "1}" : "0}"));
}

void webAdminData() {
  webSecurityHeaders(server);
  if (webAdminDenyRemote()) {
    return;
  }
  if (!admHasPass) {
    server.send(403, "application/json", "{\"nopass\":1}");
    return;
  }
  if (!webAuthed()) {
    server.send(401, "application/json", "{\"auth\":0}");
    return;
  }
  if (server.hasArg("v")) {
    webVoltageRefreshNow();
  }
  server.send(200, "application/json", webDataJson(true, !server.hasArg("l")));
}

String sysJson() {
  String j;
  j.reserve(6000);
  nvs_stats_t ns;
  bool nsOk = nvs_get_stats(NULL, &ns) == ESP_OK;
  j += "{\"up\":" + String(uptimeSec()) + ",\"boots\":" + String(bootCount) + ",\"crash\":" + String(crashTotal);
  j += ",\"reason\":\"" + jsonEsc(resetReasonText()) + "\",\"why\":\"" + jsonEsc(bootRstWhy) + "\"";
  j += ",\"heap\":" + String(ESP.getFreeHeap()) + ",\"minHeap\":" + String(ESP.getMinFreeHeap()) + ",\"maxBlock\":" + String(ESP.getMaxAllocHeap());
  j += ",\"nvsUsed\":" + String(nsOk ? (unsigned)ns.used_entries : 0) + ",\"nvsTotal\":" + String(nsOk ? (unsigned)ns.total_entries : 0);
  j += ",\"cpu\":" + String(getCpuFrequencyMhz()) + ",\"wdt\":" + String(wdtOn ? 1 : 0) + ",\"safe\":" + String(safeMode ? 1 : 0) + ",\"tls\":" + String(tlsCertFailing ? 2 : 1);
  j += ",\"adc\":{\"n\":" + String(adcHaveLast ? adcLast.n : 0) + ",\"min\":" + String(adcLast.minV) + ",\"max\":" + String(adcLast.maxV)
       + ",\"avg\":" + String((int)adcAvg(adcLast)) + ",\"sd\":" + String((int)adcStd(adcLast)) + ",\"grey\":" + String(adcLast.n ? (int)(100.0f * adcLast.grey / adcLast.n) : 0)
       + ",\"flags\":" + String(adcFlags) + "}";
  j += ",\"log\":" + logJson() + "}";
  return j;
}

void webAdminSys() {
  webSecurityHeaders(server);
  if (webAdminDenyRemote()) {
    return;
  }
  if (!admHasPass) {
    server.send(403, "application/json", "{\"nopass\":1}");
    return;
  }
  if (!webAuthed()) {
    server.send(401, "application/json", "{\"auth\":0}");
    return;
  }
  server.send(200, "application/json", sysJson());
}

void webAdminLogin() {
  webSecurityHeaders(server);
  if (webAdminDenyRemote()) {
    return;
  }
  if (!admHasPass) {
    server.send(403, "application/json", "{\"nopass\":1}");
    return;
  }
  if (webCrossOrigin()) {
    server.send(403, "application/json", "{\"ok\":0}");
    return;
  }
  const uint32_t clientIp = (uint32_t)server.client().remoteIP();
  unsigned long wait = webLockLeftSec(clientIp);
  if (wait > 0) {
    server.send(429, "application/json", "{\"ok\":0,\"wait\":" + String(wait) + "}");
    return;
  }
  if (!admCheck(server.arg("user"), server.arg("pass"))) {
    webLoginFailed(clientIp);
    logEvent(LC_WEB, LS_WARN, "Невдалий вхід в адмінпанель з " + server.client().remoteIP().toString());
    server.send(401, "application/json", "{\"ok\":0}");
    return;
  }
  webLoginSucceeded(clientIp);

  char tok[33];
  snprintf(tok, sizeof(tok), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random(),
           (unsigned long)esp_random(), (unsigned long)esp_random());
  int slot = 0;
  for (int i = 0; i < WEB_SESS_MAX; i++) {
    if (webSess[i].length() == 0) { slot = i; break; }
    if (millis() - webSessMs[i] > millis() - webSessMs[slot]) slot = i;
  }
  webSess[slot] = String(tok);
  webSessMs[slot] = millis();
  server.sendHeader("Set-Cookie", "lwsid=" + String(tok) + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=43200");
  server.send(200, "application/json", "{\"ok\":1}");
  Serial.println("Вебпанель: вхід адміністратора");
}

void webAdminLogout() {
  webSecurityHeaders(server);
  if (webAdminDenyRemote()) {
    return;
  }
  String tok = webCookieToken();
  for (int i = 0; i < WEB_SESS_MAX; i++) {
    if (tok.length() == 32 && webSess[i] == tok) {
      webSess[i] = "";
    }
  }
  server.sendHeader("Set-Cookie", "lwsid=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
  server.send(200, "application/json", "{\"ok\":1}");
}

void webAdminNotFound() {
  webSecurityHeaders(server);
  if (webAdminDenyRemote()) {
    return;
  }
  server.send(404, "text/plain", "Not found");
}

bool webPubAllowed() {
  if (!webCfOnly || !webDomOn) {
    return true;
  }
  IPAddress ip = pubServer.client().remoteIP();
  bool lan = ip[0] == 10 || ip[0] == 127 || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) || (ip[0] == 192 && ip[1] == 168) || (ip[0] == 169 && ip[1] == 254);
  if (lan || webIsCloudflareIp(ip)) {
    return true;
  }
  webSecurityHeaders(pubServer);
  pubServer.send(403, "text/plain; charset=utf-8", "Сторінка доступна лише через захищене посилання (https).");
  return false;
}

void webPubPage() {
  if (!webPubAllowed()) {
    return;
  }
  webSendPage(pubServer);
}

void webPubVoltageRefresh() {
  if (!webPubAllowed()) return;
  webSecurityHeaders(pubServer);
  bool updated = webVoltageRefreshNow();
  if (updated) {
    webPubCache = "";
    webPubLite = "";
  }
  pubServer.send(200, "application/json", String("{\"ok\":1,\"updated\":") + (updated ? "1}" : "0}"));
}

void webPubData() {
  if (!webPubAllowed()) {
    return;
  }
  webSecurityHeaders(pubServer);
  if (pubServer.hasArg("v") && webVoltageRefreshNow()) {
    webPubCache = "";
    webPubLite = "";
  }
  if (pubServer.hasArg("l")) {
    if (webPubLite.length() == 0 || millis() - webPubLiteMs >= WEB_PUB_CACHE_MS) {
      webPubLite = webDataJson(false, false);
      webPubLiteMs = millis();
    }
    pubServer.send(200, "application/json", webPubLite);
    return;
  }
  if (webPubCache.length() == 0 || millis() - webPubCacheMs >= WEB_PUB_CACHE_MS) {
    webPubCache = webDataJson(false, true);
    webPubCacheMs = millis();
  }
  pubServer.send(200, "application/json", webPubCache);
}

void webPubCheck() {
  if (!webPubAllowed()) {
    return;
  }
  webSecurityHeaders(pubServer);
  pubServer.send(200, "text/plain", "light-watcher-ok");
}

void webPubNotFound() {
  webSecurityHeaders(pubServer);
  pubServer.send(404, "text/plain", "Not found");
}

void webBegin() {
  webDomOn = preferences.getBool("webDomOn", false);
  webDomain = preferences.getString("webDomain", "");
  webIpManual = preferences.getBool("webIpMan", false);
  webCfOnly = preferences.getBool("webCfOnly", false);
  webIpVal = preferences.getString("webIpVal", "");
  admLoad();
  voltBegin();
  voltAlertLoad();
  if (safeMode) {
    Serial.println("Безпечний режим: вебсервери не запускаються");
    return;
  }

  static const char* headerKeys[] = { "Cookie", "CF-Connecting-IP", "X-Forwarded-For", "Origin", "If-None-Match" };
  server.collectHeaders(headerKeys, 5);
  server.on("/", HTTP_GET, webAdminPage);
  server.on("/favicon.ico", HTTP_GET, webAdminIcon);
  server.on("/favicon.png", HTTP_GET, webAdminIcon);
  server.on("/api/data", HTTP_GET, webAdminData);
  server.on("/api/voltage/refresh", HTTP_GET, webAdminVoltageRefresh);
  server.on("/api/sys", HTTP_GET, webAdminSys);
  server.on("/api/login", HTTP_POST, webAdminLogin);
  server.on("/api/logout", HTTP_POST, webAdminLogout);
  server.onNotFound(webAdminNotFound);
  server.begin();

  static const char* pubHeaderKeys[] = { "If-None-Match" };
  pubServer.collectHeaders(pubHeaderKeys, 1);
  pubServer.on("/", HTTP_GET, webPubPage);
  pubServer.on("/favicon.ico", HTTP_GET, webPubIcon);
  pubServer.on("/favicon.png", HTTP_GET, webPubIcon);
  pubServer.on("/api/data", HTTP_GET, webPubData);
  pubServer.on("/api/voltage/refresh", HTTP_GET, webPubVoltageRefresh);
  pubServer.on("/lw-check", HTTP_GET, webPubCheck);
  pubServer.onNotFound(webPubNotFound);
  pubServer.begin();

  webStarted = true;
  Serial.println("Вебпанель: порт 80 (адміністратор), порт 8080 (публічна сторінка)");
}

void webTick() {
  evTick();
  if (!webStarted) {
    return;
  }
  server.handleClient();
  pubServer.handleClient();
  if (powerChangePending()) {
    return;
  }
  voltMonitorTick();
  voltTick();
  publicIpTick();
  webTlsTick();
}

bool webIsCloudflareIp(const IPAddress& ip) {
  static const uint8_t nets[15][5] = {
    { 173, 245, 48, 0, 20 }, { 103, 21, 244, 0, 22 }, { 103, 22, 200, 0, 22 }, { 103, 31, 4, 0, 22 },
    { 141, 101, 64, 0, 18 }, { 108, 162, 192, 0, 18 }, { 190, 93, 240, 0, 20 }, { 188, 114, 96, 0, 20 },
    { 197, 234, 240, 0, 22 }, { 198, 41, 128, 0, 17 }, { 162, 158, 0, 0, 15 }, { 104, 16, 0, 0, 13 },
    { 104, 24, 0, 0, 14 }, { 172, 64, 0, 0, 13 }, { 131, 0, 72, 0, 22 }
  };
  uint32_t a = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | ip[3];
  for (int i = 0; i < 15; i++) {
    uint32_t n = ((uint32_t)nets[i][0] << 24) | ((uint32_t)nets[i][1] << 16) | ((uint32_t)nets[i][2] << 8) | nets[i][3];
    uint32_t mask = 0xFFFFFFFFUL << (32 - nets[i][4]);
    if ((a & mask) == (n & mask)) {
      return true;
    }
  }
  return false;
}
