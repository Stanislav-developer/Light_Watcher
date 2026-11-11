#include "lw.h"

static bool pubIpTried = false;
static bool pubIpOk = false;
static uint8_t admSalt[16];
static uint8_t admHash[32];
static uint32_t admIter = ADM_ITER;
static String webSess[WEB_SESS_MAX];
static WebFailEntry webFails[WEB_FAIL_SLOTS];
static String webPubCache;

const char lw_page_html[] PROGMEM = R"lwpage(<!DOCTYPE html><html lang="uk"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Light Watcher</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#eef2f7;font-family:-apple-system,"Segoe UI",Roboto,Arial,sans-serif;color:#1d2733;font-size:15px}
.w{max-width:920px;margin:0 auto;padding:12px}h1{font-size:21px;color:#1a56b8;margin:6px 4px 12px}
.c{background:#fff;border-radius:14px;box-shadow:0 1px 4px rgba(20,40,80,.12);padding:14px 16px;margin-bottom:12px}
.c h2{font-size:16px;color:#1a56b8;margin:0 0 10px}
table{width:100%;border-collapse:collapse;font-size:14px}td,th{padding:6px 4px;border-bottom:1px solid #eef1f5;text-align:left}
th{color:#5a6b80;font-weight:600}td.r{text-align:right}.big{font-size:22px;font-weight:700;margin-bottom:6px}
.g{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:8px}
.k{background:#f4f7fb;border-radius:10px;padding:8px 10px}.k b{display:block;font-size:17px}.k span{font-size:12px;color:#5a6b80}
.mut{color:#6b7a8c;font-size:13px}.err{color:#c62828}
.lg{margin-top:8px}.lg span{display:inline-block;margin-right:14px;font-size:13px}
.lg i{display:inline-block;width:12px;height:12px;border-radius:3px;margin-right:5px;vertical-align:-1px}
button{background:#1a56b8;color:#fff;border:0;border-radius:9px;padding:8px 13px;font-size:14px;cursor:pointer;margin:0 6px 6px 0}
button.s{background:#e3eaf5;color:#1a56b8}input{width:100%;padding:10px;border:1px solid #cdd6e2;border-radius:9px;font-size:16px;margin:4px 0 10px}
svg{width:100%;height:auto;display:block}svg rect[data-s],svg rect[data-h]{cursor:pointer}
details summary{cursor:pointer;color:#1a56b8;margin-top:10px}.sc{overflow-x:auto}

.ysEmergencyBanner{font-size:15px;line-height:1.3;padding:8px 10px!important}
.ysEmergencyBanner b{font-size:16px}
@media(max-width:600px){
  .w{padding:10px;max-width:100%;overflow-x:hidden}
  .c{padding:13px 14px;max-width:100%;min-width:0}
  .ysEmergencyBanner{font-size:13px;line-height:1.22;padding:6px 8px!important;margin:7px 0!important;border-left-width:4px!important}
  .ysEmergencyBanner b{font-size:14px;line-height:1.2}
  .ysDailyChart,.ysWeekChart{width:100%;max-width:100%;min-width:0;overflow:hidden}
  .ysDailyChart svg,.ysWeekChart svg{display:block;width:100%!important;max-width:100%;min-width:0!important;height:auto}
  .sc{max-width:100%;overflow-x:hidden}
  .g{grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
  .lg span{font-size:13px;margin-right:10px;margin-bottom:4px}
}
</style></head><body><div class="w"><h1>Light Watcher</h1><div id="app"><div class="c mut">Завантаження…</div></div></div>
<script>
var D=null,W=0,VB=[],YW=0,TZ='Europe/Kyiv';
function $(i){return document.getElementById(i)}
function esc(s){return String(s==null?'':s).replace(/[&<>"']/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]})}
function fm(t,o){try{o.timeZone=TZ;return new Intl.DateTimeFormat('uk-UA',o).format(new Date(t*1000))}catch(e){return new Date(t*1000).toLocaleString('uk-UA')}}
function hm(t){return fm(t,{hour:'2-digit',minute:'2-digit',hourCycle:'h23'})}
function fd(t){return fm(t,{day:'numeric',month:'long',year:'numeric'}).replace(/\sр\.$/,'')}
function fdIso(v){if(!/^\d{4}-\d{2}-\d{2}/.test(String(v||'')))return '';var p=String(v).slice(0,10).split('-');return fd(Date.UTC(+p[0],+p[1]-1,+p[2])/1000)}
function dm(t){return fd(t)}
function dur(s){s=Math.round(s);if(s<60)return'менше 1хв';var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+'д ':'')+(d||h?h+'год ':'')+m+'хв'}
function card(t,b){return'<div class="c"><h2>'+t+'</h2>'+b+'</div>'}
function row(a,b){return'<tr><td>'+a+'</td><td class="r">'+b+'</td></tr>'}
function kp(a,b){return'<div class="k"><span>'+a+'</span><b>'+b+'</b></div>'}
function load(){fetch('api/voltage/refresh?ts='+Date.now(),{cache:'no-store',credentials:'same-origin'}).catch(function(){}).then(function(){return fetch('api/data?ts='+Date.now(),{cache:'no-store',credentials:'same-origin'})}).then(function(r){
if(r.status==401){login('');return}if(r.status==403){nopass();return}if(!r.ok)throw 0;
return r.json().then(function(j){D=j;render();if(D.dev)loadSys()})}).catch(function(){if(D==null&&!$('lf'))$('app').innerHTML='<div class="c err">Немає зв\'язку з пристроєм. Спробуйте оновити сторінку.</div>'})}
function nopass(){D=null;$('app').innerHTML=card('🔐 Адмінпанель','<p>Пароль адміністратора ще не створено.</p><p class="mut">Створіть його в Telegram-боті: ⚙️ Налаштування → 🔐 Безпека → 🔑 Пароль адміністратора. Потім оновіть цю сторінку.</p>')}
function login(m){D=null;$('app').innerHTML=card('🔐 Вхід адміністратора','<form id="lf"><input id="u" placeholder="Логін" autocomplete="username" autocapitalize="none"><input id="p" type="password" placeholder="Пароль" autocomplete="current-password"><button>Увійти</button> <span id="lm" class="err">'+esc(m)+'</span></form>');
$('lf').onsubmit=function(e){e.preventDefault();var b='user='+encodeURIComponent($('u').value)+'&pass='+encodeURIComponent($('p').value);
fetch('api/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b,credentials:'same-origin'}).then(function(r){return r.json().then(function(j){
if(r.ok&&j.ok){$('app').innerHTML='<div class="c mut">Завантаження…</div>';load()}else if(j.wait){$('lm').textContent='Забагато невдалих спроб. Зачекайте '+j.wait+' с.'}
else if(r.status==403){nopass()}else{$('lm').textContent='Невірний логін або пароль.'}})}).catch(function(){$('lm').textContent='Помилка зв\'язку.'})}}
function evs(){return(D.ev||[]).filter(function(e){return e[0]>0}).sort(function(a,b){return a[0]-b[0]})}
function cnt(since){var E=evs(),n=0;E.forEach(function(e){if(e[1]==3&&e[0]>=since)n++});return n+((D.evFull&&E.length&&E[0][0]>since)?'+':'')}
function segs(){var o=[],cur=-2,ct=0;evs().forEach(function(e){var k=e[1],s=(k==1||k==4)?1:(k==2||k==3)?0:-1;if(cur>=0&&e[0]>ct)o.push([ct,e[0],cur]);cur=s;ct=e[0]});
if(cur>=0&&D.now>ct)o.push([ct,D.now,cur]);return o}
function today(){if(!D.wk)return 0;for(var i=0;i<14;i++)if(D.wk[i]<=D.now&&D.now<D.wk[i+1])return D.wk[i];return 0}
function vsamp(){return(D.vs||[]).filter(function(v){return v[0]>D.now-86400})}
function render(){var h=cState()+cYasno()+cStats()+cWeek()+cVolt()+cHist();if(D.dev)h+=cDev()+cSys();$('app').innerHTML=h;bind()}
var SYS=null;function loadSys(){fetch('api/sys',{cache:'no-store',credentials:'same-origin'}).then(function(r){if(!r.ok)throw 0;return r.json()}).then(function(j){SYS=j;render()}).catch(function(){})}
function cSys(){if(!SYS)return card('Система','<p class="mut">Завантаження…</p>');var s=SYS,a=s.adc,h='<table>'
+row('Працює без перезапуску',dur(s.up))+row('Запусків усього',s.boots)+row('Аварійних перезапусків',s.crash)+row('Причина останнього запуску',esc(s.reason)+(s.why?' ('+esc(s.why)+')':''))
+row('Вільна пам\'ять',Math.round(s.heap/1024)+' КБ (мін. '+Math.round(s.minHeap/1024)+' КБ)')+row('Найбільший блок',Math.round(s.maxBlock/1024)+' КБ')
+row('Сховище налаштувань',s.nvsUsed+' з '+s.nvsTotal+' записів')+row('Процесор',s.cpu+' МГц')+row('Watchdog',s.wdt?'активний, 60 с':'вимкнений')
+row('Безпечний режим',s.safe?'так':'ні')+row('Сертифікат Telegram',s.tls==2?'перевіряється, зараз не проходить перевірку':'перевіряється')
+row('Датчик світла (за хвилину)',a.n?('мін. '+a.min+', макс. '+a.max+', сер. '+a.avg+', розкид '+a.sd+', сіра зона '+a.grey+'%'):'збирається')
+row('Стан датчика',a.flags&7?'⚠️ '+((a.flags&1?'нестабільний ':'')+(a.flags&2?'біля порогів ':'')+(a.flags&4?'не змінюється':'')):'✅ норма')+'</table>';
var L=s.log||[],g='<div class="sc"><table><tr><th>Час</th><th>Категорія</th><th>Подія</th></tr>';if(!L.length)g+='<tr><td colspan="3" class="mut">Подій ще немає</td></tr>';
L.forEach(function(e){var c=e.s==2?'#c62828':e.s==1?'#b26a00':'#5a6b80';g+='<tr><td>'+(e.t?dm(e.t)+' '+hm(e.t):(e.p?'попер. сеанс, ':'')+'+'+e.up+' с')+'</td><td style="color:'+c+'">'+esc(e.c)+'</td><td style="color:'+c+'">'+esc(e.m)+'</td></tr>'});
return card('Система',h)+card('Журнал подій',g+'</table></div><p class="mut">Червоне - помилки, жовте - попередження. Зберігаються останні 60 подій; попередження й помилки переживають перезапуск.</p>')}
function cState(){var on=D.pw,h='<div class="big">'+(on?'🟢 Світло є':'🔴 Світла немає')+'</div><table>';
h+=row(on?'Світло є з':'Світла немає з',esc(D.since||'невідомо'));h+=row('Тривалість',esc(D.dur||'невідомо'));
h+=row(on?'Перед цим світла не було':'Перед цим світло було',esc(D.prev||'невідомо'));
h+=row('Напруга',on?(D.volt!=null?D.volt.toFixed(1)+' В <span class="mut">('+hm(D.voltT)+')</span>':'немає свіжих даних'):'—');
return card('Стан',h+'</table>'+(D.sync?'':'<p class="mut">⚠️ Час на пристрої ще не синхронізовано.</p>'))}
function cYasno(){
var y=D.ys||{};
if(!y.configured)return card('Графіки','<p class="mut">Адресу ще не налаштовано. Відкрийте Telegram → ⚙️ Налаштування → ⚡ Графік YASNO та введіть місто, вулицю й будинок.</p>');
var slots=YW?(y.tomorrow||[]):(y.today||[]),date=YW?y.tomorrowDate:y.todayDate,status=YW?y.tomorrowStatus:y.todayStatus;
var title=YW?'Завтра':'Сьогодні',statusText=({ScheduleApplies:'Графік застосовується',WaitingForSchedule:'Очікується уточнення графіка',EmergencyShutdowns:'Екстрені відключення',PlannedShutdowns:'Аварійні відключення',NoShutdowns:'Відключень не заплановано'}[status]||status),h='<div class="g">'+kp('Поточна група',esc(y.group||'—'))+'</div>',localDate='';try{localDate=new Intl.DateTimeFormat('en-CA',{timeZone:TZ,year:'numeric',month:'2-digit',day:'2-digit'}).format(D.now*1000)}catch(e){}var staleToday=!YW&&date&&localDate&&date.slice(0,10)!==localDate;
h+='<div style="margin:10px 0 8px"><button class="'+(YW?'s':'')+'" id="ysToday">Сьогодні</button><button class="'+(YW?'':'s')+'" id="ysTomorrow">Завтра</button></div>';
h+='<div class="mut">'+esc(title)+(date?' · '+esc(fdIso(date)):'')+(statusText?' · '+esc(statusText):'')+'</div>';
if(status==='EmergencyShutdowns')h+='<div class="k ysEmergencyBanner" style="margin:10px 0;border-left:5px solid #d32f2f;background:#fff0f0;color:#a31515"><b>🚨 ДІЮТЬ ЕКСТРЕНІ ВІДКЛЮЧЕННЯ</b><div>Плановий графік зараз не застосовується. Інтервали нижче можуть бути нечинними.</div></div>';
else if(status==='WaitingForSchedule')h+='<div class="k" style="margin:10px 0;border-left:5px solid #e59a23;background:#fff8e8;color:#8a5a00"><b>🟡 Графік очікує уточнення</b><div>YASNO може змінити часові інтервали.</div></div>';
if(!y.valid){h+='<p class="mut">Графік ще не завантажено. '+(y.error?esc(y.error):'Очікується перше оновлення.')+'</p>';return card('Графіки',h)}if(staleToday)h+='<p class="mut err">Збережений графік стосується '+esc(fdIso(date))+', а не сьогодні. Показано останні доступні дані; вони можуть бути застарілими.</p>';if(YW&&!date){h+='<p class="mut">YASNO ще не опублікував графік на завтра.</p>';return card('Графіки',h)}
var baseColor=status==='EmergencyShutdowns'?'#d6dbe2':'#2e9e4f';
 var svg='<svg class="ysDailySvg" viewBox="0 0 1000 132" role="img" aria-label="Графік YASNO за добу"><rect x="44" y="18" width="912" height="44" rx="6" fill="'+baseColor+'"/>';
(slots||[]).forEach(function(a){var st=Math.max(0,Math.min(1440,+a.s||0)),en=Math.max(st,Math.min(1440,+a.e||0));if(en<=st)return;var x=44+st/1440*912,w=(en-st)/1440*912,col=a.t==='Possible'?'#e59a23':'#d93a3a';svg+='<rect x="'+x.toFixed(1)+'" y="18" width="'+Math.max(1,w).toFixed(1)+'" height="44" rx="3" fill="'+col+'"><title>'+Math.floor(st/60).toString().padStart(2,'0')+':'+(st%60).toString().padStart(2,'0')+'–'+Math.floor(en/60).toString().padStart(2,'0')+':'+(en%60).toString().padStart(2,'0')+'</title></rect>'});
var dayStart=Date.parse(date)/1000;if(!YW&&!staleToday&&dayStart>0)(D.ev||[]).forEach(function(e){if((e[1]===3||e[1]===4)&&e[0]>=dayStart&&e[0]<dayStart+86400){var ax=44+(e[0]-dayStart)/86400*912,lab=e[1]===3?'Фактичне зникнення світла':'Фактичне відновлення світла';svg+='<line x1="'+ax.toFixed(1)+'" x2="'+ax.toFixed(1)+'" y1="10" y2="70" stroke="#2563eb" stroke-width="4"><title>'+lab+' · '+hm(e[0])+'</title></line>'}});
for(var q=0;q<=24;q+=3){var x=44+q/24*912;svg+='<line x1="'+x+'" y1="14" x2="'+x+'" y2="70" stroke="#fff" stroke-opacity=".8"/><text x="'+x+'" y="105" font-size="15" fill="#6b7a8c" text-anchor="middle">'+(q<10?'0':'')+q+':00</text>'}svg+='</svg>';
h+='<div class="sc ysDailyChart">'+svg+'</div><div class="lg"><span><i style="background:'+(status==='EmergencyShutdowns'?'#d6dbe2':'#2e9e4f')+'"></i>'+(status==='EmergencyShutdowns'?'Збережений графік (нечинний під час аварії)':'Світло за графіком')+'</span><span><i style="background:#d93a3a"></i>Підтверджене відключення</span><span><i style="background:#e59a23"></i>Можливе відключення</span><span><i style="background:#2563eb"></i>Фактична подія</span></div>';
{
 var dateLabel=date||'';
 try{if(dateLabel)dateLabel=fdIso(dateLabel)}catch(e){}
 h+='<div class="sc"><div class="mut" style="font-weight:700;margin:12px 0 7px">📅 '+esc(dateLabel||title)+'</div>';
 (slots||[]).forEach(function(a){var st=+a.s,en=+a.e;if(en<=st)return;var f=function(m){return String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0')},duration=dur((en-st)*60);h+='<div class="k" style="margin:6px 0;padding:9px 11px"><div style="font-weight:700">'+f(st)+' – '+f(en)+' ('+duration+')</div></div>'});
 h+='</div>'
}
if(!YW&&D.sync&&!staleToday&&status!=='EmergencyShutdowns'&&status!=='WaitingForSchedule'){var pp=0;try{var parts=new Intl.DateTimeFormat('en-GB',{timeZone:TZ,hour:'2-digit',minute:'2-digit',hourCycle:'h23'}).format(D.now*1000).split(':');pp=(+parts[0])*60+(+parts[1])}catch(e){}var currentSlot=(slots||[]).find(function(a){return pp>=+a.s&&pp<+a.e}),plannedOff=!!currentSlot,actualOn=!!D.pw,possible=currentSlot&&currentSlot.t==='Possible',match=(plannedOff?!actualOn:actualOn);h+='<div class="k" style="margin-top:10px"><b>План vs факт зараз</b><div class="mut">За YASNO: '+(possible?'🟠 можливе відключення':(plannedOff?'🔴 підтверджене відключення':'🟢 відключення не заплановане'))+'</div><div class="mut">Фактично: '+(actualOn?'🟢 світло є':'🔴 світла немає')+'</div><div style="margin-top:5px;color:'+(possible?'#b26a00':(match?'#16803c':'#b26a00'))+';font-weight:600">'+(possible?'Період можливого відключення — порівняння попереднє':(match?'Стан відповідає графіку':'Є відхилення від графіка'))+'</div></div>'}
if(y.error)h+='<p class="mut err">Остання помилка оновлення: '+esc(y.error)+'</p>';
return card('Плановий графік YASNO', '<p class="mut">Це розклад від YASNO, а не журнал фактичних відключень. Очищення статистики не видаляє плановий графік.</p>'+h)}
function cStats(){var s=D.st,t0=today(),v=vsamp().filter(function(x){return t0&&x[0]>=t0}),mn=null,mx=null;
v.forEach(function(x){if(mn==null||x[1]<mn)mn=x[1];if(mx==null||x[1]>mx)mx=x[1]});
var completed=Math.max(0,(s.total||0)-(s.open?1:0));
var h='<div class="g">'+(D.sync?kp('Подій зникнення за 24 год',cnt(D.now-86400))+kp('За 7 днів',cnt(D.now-604800))+kp('За 30 днів',cnt(D.now-2592000)):'');
h+=kp('Завершених відключень',completed)+kp('Поточне відключення',s.open?esc(D.dur||'триває'):'—')+kp('Найдовше відключення',s.exact?dur(s.maxOff):'—')+kp('Сумарно без світла',dur(s.offSum))+kp('Сумарно зі світлом',dur(s.onSum));
h+=kp('Мін. напруга сьогодні',mn!=null?(mn/10).toFixed(1)+' В':'—')+kp('Макс. напруга сьогодні',mx!=null?(mx/10).toFixed(1)+' В':'—')+'</div>';
if(D.evFull)h+='<p class="mut">«+» - журнал подій зберігає останні записи; старіші вже витіснено.</p>';return card('Статистика',h)}
function cWeek(){var T='Фактична історія електропостачання за тиждень';if(!D.sync||!D.wk)return card(T,'<p class="mut">Час на пристрої ще не синхронізовано.</p>');
var b=W?0:7,S=segs(),now=D.now,rh=52,lw=108,cw=880,H=7*rh+26,on=0,off=0,n=0,g='<svg class="ysWeekSvg" viewBox="0 0 '+(lw+cw+22)+' '+H+'">';
for(var i=0;i<7;i++){var d0=D.wk[b+i],d1=D.wk[b+i+1],L=d1-d0,y=i*rh;
g+='<text x="0" y="'+(y+32)+'" font-size="16" fill="#1d2733">'+esc(D.dl[b+i])+'</text><rect x="'+lw+'" y="'+(y+4)+'" width="'+cw+'" height="'+(rh-8)+'" rx="4" fill="#d6dbe2"/>';
S.forEach(function(s){var a=Math.max(s[0],d0),z=Math.min(s[1],d1,now);if(z<=a)return;if(s[2])on+=z-a;else off+=z-a;
g+='<rect data-s="'+a+','+z+','+s[2]+'" x="'+(lw+(a-d0)/L*cw).toFixed(1)+'" y="'+(y+4)+'" width="'+Math.max((z-a)/L*cw,1).toFixed(1)+'" height="'+(rh-8)+'" fill="'+(s[2]?'#2e9e4f':'#d93a3a')+'"/>'});
evs().forEach(function(e){var t=e[0],k=e[1];if(t<d0||t>=d1||t>now)return;
var color=k==3?'#b91c1c':k==4?'#15803d':k==5?'#7c3aed':(k==1||k==2)?'#2563eb':'';
if(!color)return;
var x=lw+(t-d0)/L*cw, label=k==3?'Зникнення електроенергії':k==4?'Відновлення електроенергії':k==5?'Пристрій зупинився / кінець сеансу':(k==1?'Запуск пристрою: світло є':'Запуск пристрою: світла немає');
g+='<rect x="'+(x-2.5).toFixed(1)+'" y="'+(y+4)+'" width="5" height="'+(rh-8)+'" fill="'+color+'"><title>'+label+' — '+dm(t)+' '+hm(t)+'</title></rect>';
});}
for(var q=0;q<=24;q+=3){var x=lw+q/24*cw;g+='<line x1="'+x+'" y1="2" x2="'+x+'" y2="'+(7*rh-2)+'" stroke="#fff" stroke-opacity=".75"/><text x="'+x+'" y="'+(7*rh+18)+'" font-size="14" fill="#6b7a8c" text-anchor="middle">'+(q<10?'0':'')+q+':00</text>'}
evs().forEach(function(e){if(e[1]==3&&e[0]>=D.wk[b]&&e[0]<D.wk[b+7])n++});
var h='<p class="mut">Це фактична історія за журналом подій ESP32, а не план YASNO. Після очищення зберігається лише поточне активне відключення та нові події.</p><button class="'+(W?'':'s')+'" id="wp">◀ Попередній</button><button class="'+(W?'s':'')+'" id="wc">Поточний ▶</button>';
h+='<div class="mut">'+esc(fd(D.wk[b]))+' – '+esc(fd(D.wk[b+6]))+', Київ</div><div class="sc ysWeekChart">'+g+'</svg></div>';
h+='<div class="lg"><span><i style="background:#2e9e4f"></i>Світло є</span><span><i style="background:#d93a3a"></i>Світла немає</span><span><i style="background:#d6dbe2"></i>Немає даних / пристрій вимкнений</span></div>';
h+='<div class="lg"><span><i style="background:#b91c1c"></i>Подія: зникнення</span><span><i style="background:#15803d"></i>Подія: відновлення</span><span><i style="background:#7c3aed"></i>Зупинка пристрою</span><span><i style="background:#2563eb"></i>Запуск / перезапуск</span></div>';
h+='<div id="ws" class="mut">Натисніть на відрізок, щоб побачити час.</div><div class="g" style="margin-top:8px">'+kp('Зі світлом',dur(on))+kp('Без світла',dur(off))+kp('Відключень',n)+'</div>';return card(T,h)}
function cVolt(){var vs=vsamp(),lo=D.lim[0],hi=D.lim[1],h='<p class="mut">Допустимі межі: '+lo+'–'+hi+' В. При відкритій сторінці Shelly опитується під час завантаження та приблизно кожні 30 с; у фоновому режимі — кожні 5 хв.</p>';VB=[];
if(!vs.length)h+='<p>Немає вимірювань Shelly за останні 24 години.</p>';else{
var st=Math.floor(D.now/3600)*3600-82800,mn=1e9,mx=0,mnT=0,mxT=0;for(var i=0;i<24;i++)VB.push([]);
vs.forEach(function(v){var i=Math.floor((v[0]-st)/3600);if(i>=0&&i<24)VB[i].push(v);if(v[1]<mn){mn=v[1];mnT=v[0]}if(v[1]>mx){mx=v[1];mxT=v[0]}});
var y0=Math.min(lo-8,mn/10-3),y1=Math.max(hi+8,mx/10+3),X=50,PW=940,PH=200,bw=PW/24;function Y(v){return 10+(y1-v)/(y1-y0)*PH}
var g='<svg viewBox="0 0 1000 '+(PH+40)+'">';[lo,hi].forEach(function(l){g+='<line x1="'+X+'" x2="'+(X+PW)+'" y1="'+Y(l)+'" y2="'+Y(l)+'" stroke="#d93a3a" stroke-dasharray="6 4"/><text x="'+(X-6)+'" y="'+(Y(l)+4)+'" font-size="12" fill="#d93a3a" text-anchor="end">'+l+'</text>'});
VB.forEach(function(B,i){if(!B.length)return;var a=1e9,z=0;B.forEach(function(v){if(v[1]<a)a=v[1];if(v[1]>z)z=v[1]});var top=Y(z/10),bot=Y(a/10),out=(a/10<lo||z/10>hi);
g+='<rect data-h="'+i+'" x="'+(X+i*bw+3).toFixed(1)+'" y="'+top.toFixed(1)+'" width="'+(bw-6).toFixed(1)+'" height="'+Math.max(bot-top,3).toFixed(1)+'" rx="2" fill="'+(out?'#d93a3a':'#2f6fd6')+'"/>'});
for(var q=0;q<=24;q+=4)g+='<text x="'+(X+q*bw)+'" y="'+(PH+30)+'" font-size="12" fill="#6b7a8c" text-anchor="middle">'+hm(st+q*3600)+'</text>';
h+='<div class="sc">'+g+'</svg></div><div class="lg"><span><i style="background:#2f6fd6"></i>У межах</span><span><i style="background:#d93a3a"></i>Вихід за межі</span><span>Стовпчик - мінімум…максимум за годину</span></div>';
h+='<div class="g" style="margin-top:8px">'+kp('Мінімум за 24 год',(mn/10).toFixed(1)+' В <span class="mut">'+hm(mnT)+'</span>')+kp('Максимум за 24 год',(mx/10).toFixed(1)+' В <span class="mut">'+hm(mxT)+'</span>')+kp('Вимірювань',vs.length)+'</div>';
h+='<div id="vh" class="mut" style="margin-top:6px">Натисніть на стовпчик, щоб побачити вимірювання за цю годину.</div>'}
var sp=(D.sp||[]).slice().reverse();h+='<details><summary>Історія стрибків напруги ('+sp.length+')</summary>';
if(!sp.length)h+='<p class="mut">Виходів напруги за межі не зафіксовано.</p>';else{h+='<div class="sc"><table><tr><th>Час</th><th>Значення</th><th>Тип</th><th>Відхилення</th></tr>';
sp.forEach(function(s){var v=s[1]/10,low=v<s[2];h+='<tr><td>'+dm(s[0])+' '+hm(s[0])+'</td><td>'+v.toFixed(1)+' В</td><td>'+(low?'мінімум (нижче межі)':'максимум (вище межі)')+'</td><td>'+(low?'−':'+')+Math.abs(v-s[2]).toFixed(1)+' В від '+s[2]+' В</td></tr>'});h+='</table></div>'}
return card('Напруга за 24 години',h+'</details>')}
function outs(){var o=[],c=null;evs().forEach(function(e){var k=e[1],t=e[0];
if(k==3){if(c)o.push(c);c={a:t,b:0}}else if(k==4){if(c){c.b=t;o.push(c);c=null}}else if(k==1){if(c){c.u=1;o.push(c);c=null}}else if(k==2){if(!c)c={a:0,b:0}}});
if(c){c.o=1;o.push(c)}return o.reverse().slice(0,60)}
function cHist(){var O=outs(),h;if(!O.length)return card('Історія відключень','<p class="mut">Відключень ще не зафіксовано.</p>');
h='<div class="sc"><table><tr><th>Дата</th><th>Початок</th><th>Кінець</th><th>Тривалість</th></tr>';
O.forEach(function(x){var e=x.o?'триває':(x.b?((x.a&&dm(x.a)!=dm(x.b))?dm(x.b)+' ':'')+hm(x.b):'невідомо');
var d=x.o?(x.a?dur(D.now-x.a)+' (триває)':'—'):((x.a&&x.b)?dur(x.b-x.a):'невідомо');
h+='<tr><td>'+dm(x.a||x.b||D.now)+'</td><td>'+(x.a?hm(x.a):'невідомо')+'</td><td>'+e+'</td><td>'+d+'</td></tr>'});return card('Історія відключень',h+'</table></div>')}
function cDev(){var v=D.dev,h='<table>'+row('Версія прошивки',esc(v.ver))+row('Локальна IP',esc(v.ip))+row('Wi-Fi мережа',esc(v.ssid))+row('Сигнал Wi-Fi',v.rssi+' dBm')+row('Працює без перезапуску',dur(v.up))
+row('Вільна пам\'ять',Math.round(v.heap/1024)+' КБ')+row('Shelly IP',esc(v.shelly||'не задано'))+row('Telegram',v.tg?'на зв\'язку':'недоступний')+row('Груп і каналів',v.chats)
+row('Публічна сторінка',v.link?'<a href="'+esc(v.link)+'" target="_blank" rel="noopener">'+esc(v.link)+'</a>':'не налаштовано')+'</table>';
return card('Інформація про пристрій',h+'<p><button class="s" id="lo">Вийти</button></p>')}
function bind(){var a=$('wp'),b=$('wc'),l=$('lo'),yt=$('ysToday'),ym=$('ysTomorrow');if(yt)yt.onclick=function(){YW=0;render()};if(ym)ym.onclick=function(){YW=1;render()};if(a)a.onclick=function(){W=1;render()};if(b)b.onclick=function(){W=0;render()};
if(l)l.onclick=function(){fetch('api/logout',{method:'POST',credentials:'same-origin'}).then(function(){login('')})};
document.querySelectorAll('rect[data-s]').forEach(function(r){r.onclick=function(){var p=r.getAttribute('data-s').split(','),x=+p[0],z=+p[1];
$('ws').textContent=dm(x)+' '+hm(x)+'–'+hm(z)+' '+(p[2]=='1'?'🟢 світло є':'🔴 світла немає')+' ('+dur(z-x)+')'}});
document.querySelectorAll('rect[data-h]').forEach(function(r){r.onclick=function(){var B=VB[+r.getAttribute('data-h')];
$('vh').innerHTML=B.map(function(v){return hm(v[0])+' - <b>'+(v[1]/10).toFixed(1)+' В</b>'}).join(' · ')}})}
setInterval(function(){if(D)load()},30000);load();
</script></body></html>)lwpage";

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

      logEvent(LC_WEB, LS_WARN, "Публічна IP змінилася: " + pubIpAuto + " -> " + ip + ". Оновіть запис A в Cloudflare");
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
    logEvent(LC_WEB, LS_ERR, "Пароль адмінпанелі: помилка обчислення хешу (mbedTLS)");
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

String webDataJson(bool admin) {
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

  j += ",\"ys\":{\"configured\":" + String(yasnoGroup.length() ? "true" : "false");
  j += ",\"valid\":" + String(yasnoScheduleValid ? "true" : "false");
  j += ",\"city\":\"" + jsonEsc(yasnoCity) + "\",\"group\":\"" + jsonEsc(yasnoGroup) + "\"";
  j += ",\"today\":" + (yasnoScheduleValid ? yasnoTodaySlotsJson : String("[]"));
  j += ",\"tomorrow\":" + (yasnoScheduleValid ? yasnoTomorrowSlotsJson : String("[]"));
  j += ",\"todayDate\":\"" + jsonEsc(yasnoTodayDate) + "\",\"tomorrowDate\":\"" + jsonEsc(yasnoTomorrowDate) + "\"";
  j += ",\"todayStatus\":\"" + jsonEsc(yasnoTodayStatus) + "\",\"tomorrowStatus\":\"" + jsonEsc(yasnoTomorrowStatus) + "\"";
  j += ",\"updatedOn\":\"" + jsonEsc(yasnoScheduleUpdatedOn) + "\",\"fetchedAt\":" + String(yasnoScheduleFetchedAt);
  j += ",\"error\":\"" + (admin ? jsonEsc(yasnoTaskErrorCopy) : String(yasnoTaskErrorCopy.length() ? "Не вдалося оновити графік" : "")) + "\"}";
  if (admin) {
    j += ",\"dev\":{\"ver\":\"V2.4.11\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"ssid\":\"" + jsonEsc(WiFi.SSID()) + "\",\"rssi\":" + String(WiFi.RSSI())
         + ",\"up\":" + String(uptimeSec()) + ",\"heap\":" + String(ESP.getFreeHeap()) + ",\"shelly\":\"" + jsonEsc(shellyIP) + "\""
         + ",\"tg\":" + String(telegramOnline ? 1 : 0) + ",\"chats\":" + String(chatActiveCount()) + ",\"link\":\"" + jsonEsc(webPublicLink()) + "\"}";
  }
  j += "}";
  return j;
}

void webAdminPage() {
  webSecurityHeaders(server);
  if (webAdminDenyRemote()) {
    return;
  }
  server.send_P(200, "text/html; charset=utf-8", lw_page_html);
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
  server.send(200, "application/json", webDataJson(true));
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
  webSecurityHeaders(pubServer);
  pubServer.send_P(200, "text/html; charset=utf-8", lw_page_html);
}

void webPubVoltageRefresh() {
  if (!webPubAllowed()) return;
  webSecurityHeaders(pubServer);
  bool updated = webVoltageRefreshNow();
  if (updated) webPubCache = "";
  pubServer.send(200, "application/json", String("{\"ok\":1,\"updated\":") + (updated ? "1}" : "0}"));
}

void webPubData() {
  if (!webPubAllowed()) {
    return;
  }
  webSecurityHeaders(pubServer);
  if (webPubCache.length() == 0 || millis() - webPubCacheMs >= 5000UL) {
    webPubCache = webDataJson(false);
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

  static const char* headerKeys[] = { "Cookie", "CF-Connecting-IP", "X-Forwarded-For", "Origin" };
  server.collectHeaders(headerKeys, 4);
  server.on("/", HTTP_GET, webAdminPage);
  server.on("/api/data", HTTP_GET, webAdminData);
  server.on("/api/voltage/refresh", HTTP_GET, webAdminVoltageRefresh);
  server.on("/api/sys", HTTP_GET, webAdminSys);
  server.on("/api/login", HTTP_POST, webAdminLogin);
  server.on("/api/logout", HTTP_POST, webAdminLogout);
  server.onNotFound(webAdminNotFound);
  server.begin();

  pubServer.on("/", HTTP_GET, webPubPage);
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
