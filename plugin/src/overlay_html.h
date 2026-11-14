#pragma once
// The OBS browser-source page served at http://127.0.0.1:<port>/.
// URL options (all optional):
//   show=unique,set,rune,orb,rare,magic,crafted,base   which drops to show (default unique,set,rune,orb)
//                   orb = the seven Reimagined orbs (Corruption, Socketing, Conversion,
//                   Assemblage, Infusion, Shadows, Renewal) and their stacks
//   minrune=21      lowest rune number shown (1 = El ... 20 = Lem, 21 = Pul ... 33 = Zod).
//                   Default 21: only runes above 20.
//   duration=12     seconds each card stays
//   max=6           cards on screen at once
//   align=left|right  side of the screen the stack grows from
//   scale=1         size multiplier
//   replay=0        drops replayed when the source (re)loads
//   test=1          show a sample card on load, to position the source in OBS
// Animations use opacity/transform only; there is no idle animation loop.

namespace LootDrop {

inline constexpr const char* OverlayHtml = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Loot Drops</title>
<style>
:root{--unique:#c7b377;--set:#1ec81e;--rune:#ffa800;--orb:#ff8c1a;--rare:#ffff64;--magic:#7878ff;--crafted:#ff9a2e;--base:#e8e8e8;--eth:#9fb4c8;--scale:1}
html,body{margin:0;background:transparent;overflow:hidden}
body{font-family:"Palatino Linotype","Book Antiqua",Palatino,Georgia,serif;font-size:calc(22px*var(--scale))}
#stack{position:fixed;bottom:calc(24px*var(--scale));left:calc(24px*var(--scale));display:flex;flex-direction:column-reverse;gap:calc(10px*var(--scale));align-items:flex-start}
body.right #stack{left:auto;right:calc(24px*var(--scale));align-items:flex-end}
.card{display:flex;align-items:center;gap:.6em;padding:.45em .9em .45em .7em;background:rgba(8,6,4,.86);border:1px solid rgba(199,179,119,.35);border-left:4px solid var(--c);border-radius:4px;
 box-shadow:0 4px 18px rgba(0,0,0,.55);color:var(--c);opacity:0;transform:translateX(-30px);animation:in .35s ease-out forwards;will-change:transform,opacity;max-width:calc(560px*var(--scale))}
body.right .card{border-left:1px solid rgba(199,179,119,.35);border-right:4px solid var(--c);transform:translateX(30px)}
.card.out{animation:out .6s ease-in forwards}
.card.big{animation:in .35s ease-out forwards,pulse 1.2s ease-out .35s 2}
.gem{width:.7em;height:.7em;transform:rotate(45deg);background:var(--c);box-shadow:0 0 10px var(--c);flex:none}
.card.orb .gem{transform:none;border-radius:50%}
.txt{display:flex;flex-direction:column;line-height:1.15;min-width:0}
.name{font-weight:bold;letter-spacing:.02em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;text-shadow:0 0 6px rgba(0,0,0,.9)}
.sub{font-size:.62em;color:#bdb6a4;letter-spacing:.06em;text-transform:uppercase;white-space:nowrap}
.tag{color:var(--eth)}
@keyframes in{to{opacity:1;transform:none}}
@keyframes out{from{opacity:1;transform:none}to{opacity:0;transform:translateY(12px)}}
@keyframes pulse{0%{transform:scale(1)}30%{transform:scale(1.06)}100%{transform:scale(1)}}
</style></head>
<body><div id="stack"></div>
<script>
(function(){
const q=new URLSearchParams(location.search);
const show=new Set((q.get('show')||'unique,set,rune,orb').toLowerCase().split(',').map(s=>s.trim()).filter(Boolean));
const minRune=Math.max(1,Math.min(33,parseInt(q.get('minrune')||'21',10)||21));
const duration=Math.max(2,parseFloat(q.get('duration')||'12')||12)*1000;
const max=Math.max(1,Math.min(20,parseInt(q.get('max')||'6',10)||6));
const scale=Math.max(.4,Math.min(4,parseFloat(q.get('scale')||'1')||1));
const replay=Math.max(0,Math.min(50,parseInt(q.get('replay')||'0',10)||0));
document.documentElement.style.setProperty('--scale',scale);
if((q.get('align')||'').toLowerCase()==='right')document.body.classList.add('right');
const stack=document.getElementById('stack');
const BIG_RUNE=26; // Vex and above get the pulse
// Reimagined's seven orbs and their stack items (misc.txt codes).
const ORBS=new Set(['ka3','oos','ooc','ooa','ooi','ooe','oor','1ka','1os','1oc','1oa','1oi','1oe','1or']);
function kind(d){if(d.rune>0)return'rune';if(ORBS.has(d.code))return'orb';const k=d.quality;return(k==='inferior'||k==='normal'||k==='superior')?'base':k;}
function wanted(d){const k=kind(d);if(!show.has(k))return false;if(k==='rune'&&d.rune<minRune)return false;return true;}
function el(tag,cls,text){const e=document.createElement(tag);if(cls)e.className=cls;if(text!==undefined)e.textContent=text;return e;}
function add(d){
 if(!wanted(d))return;
 const k=kind(d);
 const card=el('div','card');
 card.style.setProperty('--c','var(--'+k+')');
 if(k==='orb')card.classList.add('orb');
 if(k==='unique'||k==='set'||(k==='rune'&&d.rune>=BIG_RUNE))card.classList.add('big');
 card.appendChild(el('div','gem'));
 const t=el('div','txt');
 t.appendChild(el('div','name',d.name||d.base||d.code));
 const sub=el('div','sub');
 const parts=[];
 if(k!=='rune'&&d.base&&d.base!==d.name)parts.push(d.base);
 parts.push(k==='base'?d.quality:k);
 sub.textContent=parts.join(' · ');
 if(d.ethereal){sub.appendChild(document.createTextNode(' · '));sub.appendChild(el('span','tag','ethereal'));}
 if(d.test){sub.appendChild(document.createTextNode(' · test'));}
 t.appendChild(sub);card.appendChild(t);
 stack.appendChild(card);
 while(stack.children.length>max)stack.removeChild(stack.firstChild);
 setTimeout(()=>{card.classList.add('out');setTimeout(()=>card.remove(),650);},duration);
}
if(q.get('test')==='1')add({name:'Harlequin Crest',base:'Shako',quality:'unique',rune:0,code:'uap',test:true});
let es,lastSeq=0,lastTime=0;
// A replayed event after a reconnect has a seq we already showed. A game restart resets
// seq to 1, so a lower seq with a newer timestamp is a new drop.
function connect(){
 es=new EventSource('/events?replay='+replay);
 es.addEventListener('drop',ev=>{try{const d=JSON.parse(ev.data);
  const restart=d.seq<lastSeq&&d.time>lastTime;
  if(d.seq<=lastSeq&&!restart)return;
  lastSeq=d.seq;lastTime=Math.max(lastTime,d.time);add(d);}catch(e){}});
 es.onerror=()=>{}; // EventSource retries by itself (retry: 2000 from the server)
}
connect();
})();
</script></body></html>)HTML";

}
