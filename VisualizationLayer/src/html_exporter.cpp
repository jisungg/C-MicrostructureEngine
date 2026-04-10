#include "visualization/html_exporter.hpp"

#include <cstdio>
#include <fstream>
#include <string>

#include "visualization/export_schema.hpp"
#include "visualization/json_serializer.hpp"

namespace visualization {

// ── HTML template ─────────────────────────────────────────────────────────
// NOTE: raw string literals use the R"VHTML(...)VHTML" delimiter to avoid
// premature termination on ')' + '"' sequences inside HTML onclick attributes.

std::string HtmlExporter::build_html(const std::string& frames_json,
                                     std::size_t        frame_count)
{
    char cnt_c[64], ver_c[64];
    std::snprintf(cnt_c, sizeof(cnt_c), "<!-- frame_count:%zu -->", frame_count);
    std::snprintf(ver_c, sizeof(ver_c), "<!-- schema_version:%d -->", kSchemaVersion);

    std::string html;
    html.reserve(196608);

    // ── Head + CSS ────────────────────────────────────────────────────────
    html += R"VHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Microstructure Replay</title>
<style>
:root{
  --bg:#0B0C10;--panel:#111318;--border:#1E2028;
  --bid:#2ECC71;--ask:#FF5C5C;--accent:#4DA3FF;
  --text:#E6E8EC;--muted:#6B7280;--trade:#F59E0B;
  --tight:#2ECC71;--normal:#4DA3FF;--stressed:#F59E0B;--illiquid:#FF5C5C
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%;background:var(--bg);color:var(--text);
  font-family:'SF Mono','Roboto Mono',Consolas,monospace;font-size:12px;overflow:hidden}
/* three-zone grid */
.app{display:grid;grid-template-rows:44px 1fr 52px;height:100vh}
/* top bar */
.topbar{display:flex;align-items:center;gap:5px;padding:0 10px;
  background:var(--panel);border-bottom:1px solid var(--border);overflow:hidden;flex-shrink:0}
.tb-title{color:var(--accent);font-size:12px;font-weight:700;letter-spacing:1px;
  white-space:nowrap;margin-right:4px;flex-shrink:0}
.sep{color:var(--border);padding:0 1px;flex-shrink:0}
.btn{background:#1A1D24;color:var(--text);border:1px solid var(--border);
  padding:3px 8px;cursor:pointer;border-radius:3px;font-family:inherit;
  font-size:11px;transition:background .1s;flex-shrink:0}
.btn:hover{background:#252933}
.btn.active{background:#1B2A3F;border-color:var(--accent);color:var(--accent)}
.ctrl-grp{display:flex;align-items:center;gap:3px;flex-shrink:0}
.lbl{color:var(--muted);font-size:10px;white-space:nowrap;flex-shrink:0}
input[type=range].sp-sl{width:64px;height:3px;cursor:pointer;accent-color:var(--accent)}
#spd{color:var(--muted);font-size:10px;white-space:nowrap;min-width:36px}
input[type=number].ji{width:60px;background:#1A1D24;color:var(--text);
  border:1px solid var(--border);padding:3px 4px;border-radius:3px;
  font-family:inherit;font-size:11px}
select.flt{background:#1A1D24;color:var(--text);border:1px solid var(--border);
  padding:3px 4px;border-radius:3px;font-family:inherit;font-size:11px;cursor:pointer}
.fi{color:var(--muted);font-size:10px;margin-left:auto;white-space:nowrap;
  overflow:hidden;text-overflow:ellipsis;flex-shrink:1}
/* content area */
.content{display:grid;grid-template-columns:300px 1fr;overflow:hidden;min-height:0}
/* left column */
.lcol{display:flex;flex-direction:column;border-right:1px solid var(--border);
  overflow:hidden;min-height:0}
/* panel header */
.phdr{display:flex;align-items:center;justify-content:space-between;
  padding:4px 8px;background:#0D0F14;border-bottom:1px solid var(--border);flex-shrink:0}
.ptitle{color:var(--muted);font-size:9px;font-weight:700;
  letter-spacing:.8px;text-transform:uppercase}
/* order book */
.book-panel{flex:1 1 0;min-height:0;display:flex;flex-direction:column;overflow:hidden}
.book-body{display:flex;flex-direction:column;flex:1 1 0;min-height:0;
  overflow:hidden;padding:2px 0}
.ask-side{flex:1 1 0;overflow:hidden;display:flex;flex-direction:column;
  justify-content:flex-end}
.bid-side{flex:1 1 0;overflow:hidden}
.spread-band{background:#0D0F14;text-align:center;padding:3px 8px;font-size:11px;
  color:#E6B450;border-top:1px solid var(--border);border-bottom:1px solid var(--border);
  flex-shrink:0}
/* level rows */
.lr{display:flex;align-items:center;padding:1px 8px;gap:4px;position:relative;cursor:default}
.lr:hover{background:#1A1D24}
.trade-level{background:#1E1205;box-shadow:inset 2px 0 0 var(--trade)}
.lbar{position:absolute;top:0;bottom:0;right:0;pointer-events:none;transition:width .12s ease}
.ask-l .lbar{background:rgba(255,92,92,.13)}
.bid-l .lbar{background:rgba(46,204,113,.13)}
.lp{min-width:60px;text-align:right;font-weight:500;font-size:11px;
  position:relative;z-index:1}
.ask-l .lp{color:var(--ask)}
.bid-l .lp{color:var(--bid)}
.lv{min-width:56px;text-align:right;color:var(--muted);font-size:11px;
  position:relative;z-index:1}
.lq{min-width:22px;text-align:right;color:#3A3E4A;font-size:9px;
  position:relative;z-index:1}
.lx{color:#555;font-size:9px;display:none;margin-left:auto;
  position:relative;z-index:1;white-space:nowrap}
.lr:hover .lx{display:inline}
/* trade badge */
.tbadge{background:#2A1A05;border:1px solid var(--trade);color:var(--trade);
  padding:1px 6px;border-radius:3px;font-size:10px;display:inline-block;
  animation:pulse .4s ease}
@keyframes pulse{
  0%{box-shadow:0 0 0 0 rgba(245,158,11,.4)}
  70%{box-shadow:0 0 0 5px rgba(245,158,11,0)}
  100%{box-shadow:0 0 0 0 rgba(245,158,11,0)}}
/* signals */
.sigs-panel{flex:0 0 auto;background:var(--panel);border-top:1px solid var(--border);
  overflow:auto}
.sr{display:flex;justify-content:space-between;align-items:center;
  padding:2px 8px;border-bottom:1px solid #0D0F14}
.sl{color:var(--muted);font-size:10px}
.sv{color:#80CBC4;font-size:10px;font-variant-numeric:tabular-nums}
.regime-tight{color:var(--tight)}
.regime-normal{color:var(--normal)}
.regime-stressed{color:var(--stressed)}
.regime-illiquid{color:var(--illiquid)}
/* right column */
.rcol{display:flex;flex-direction:column;overflow:hidden;min-height:0}
/* 6 charts: 2 cols x 3 rows */
.charts-grid{flex:1 1 0;min-height:0;display:grid;
  grid-template-columns:1fr 1fr;grid-template-rows:1fr 1fr 1fr;gap:0;overflow:hidden}
.chart-cell{background:var(--panel);border-right:1px solid var(--border);
  border-bottom:1px solid var(--border);display:flex;flex-direction:column;
  overflow:hidden;min-height:0}
.chdr{padding:2px 7px;background:#0D0F14;border-bottom:1px solid var(--border);
  display:flex;justify-content:space-between;align-items:center;flex-shrink:0}
.ctitle{color:var(--muted);font-size:9px;letter-spacing:.5px;text-transform:uppercase}
.cval{color:var(--text);font-size:10px;font-variant-numeric:tabular-nums}
.chart-cell canvas{flex:1 1 0;display:block;width:100%;min-height:0}
/* liquidity heatmap panel */
.hm-panel{flex:0 0 0;overflow:hidden;background:var(--panel);
  border-top:1px solid var(--border);transition:flex-basis .18s ease;
  display:flex;flex-direction:column}
.hm-panel.vis{flex:0 0 116px}
#hm-canvas{flex:1 1 0;display:block;width:100%;min-height:0}
/* event tape */
.tape{flex:0 0 82px;overflow-y:auto;background:var(--panel);
  border-top:1px solid var(--border)}
.tape-row{display:flex;align-items:center;padding:1px 8px;gap:6px;
  font-size:10px;cursor:pointer;border-bottom:1px solid #0D0F14;
  transition:background .08s}
.tape-row:hover{background:#1A1D24}
.tape-row.cur{background:#1B2A3F}
.tet{min-width:52px;font-weight:500}
.et-ADD{color:var(--bid)}.et-CANCEL{color:#6B7280}.et-MODIFY{color:#A78BFA}
.et-TRADE{color:var(--trade)}.et-SNAPSHOT{color:var(--accent)}
.tep{min-width:50px;text-align:right;color:var(--muted)}
.teid{min-width:36px;text-align:right;color:#3A3E4A;font-size:9px}
/* scrubber */
.scr-bar{background:#0A0B0E;border-top:1px solid var(--border);
  padding:5px 10px 3px;display:flex;flex-direction:column;gap:3px}
#scrubber{display:block;width:100%;height:26px;cursor:pointer;
  touch-action:none;border-radius:2px}
.scr-lbl{display:flex;justify-content:space-between;color:var(--muted);font-size:9px}
/* bookmarks */
.bk-panel{flex:0 0 auto;background:var(--panel);border-top:1px solid var(--border);
  max-height:80px;overflow-y:auto}
.bk-row{display:flex;align-items:center;padding:2px 8px;gap:6px;font-size:10px;
  cursor:pointer;border-bottom:1px solid #0D0F14}
.bk-row:hover{background:#1A1D24}
.bk-cur{background:#2A1A05}
.bk-dot{width:7px;height:7px;border-radius:50%;background:var(--trade);flex-shrink:0}
/* search overlay */
#srch-overlay{display:none;position:fixed;top:0;left:0;right:0;bottom:0;
  background:rgba(0,0,0,.72);z-index:9999;align-items:flex-start;justify-content:center;
  padding-top:80px}
#srch-overlay.vis{display:flex}
#srch-box{background:#1A1D24;border:1px solid var(--accent);border-radius:5px;
  padding:14px 16px;width:360px;display:flex;flex-direction:column;gap:8px;
  box-shadow:0 8px 32px rgba(0,0,0,.6)}
#srch-input{background:#0B0C10;color:var(--text);border:1px solid var(--border);
  border-radius:3px;padding:7px 10px;font-family:inherit;font-size:13px;width:100%}
#srch-hint{color:var(--muted);font-size:10px}
#srch-result{color:var(--accent);font-size:11px;min-height:14px}
/* tape filter */
#tape-flt{display:block;background:#1A1D24;color:var(--text);
  border:none;border-bottom:1px solid var(--border);
  padding:3px 8px;font-family:inherit;font-size:10px;width:100%}
#tape-flt:focus{outline:none;border-bottom-color:var(--accent)}
/* scrubber tick marks */
#scr-wrap{position:relative;width:100%}
#bk-ticks{position:absolute;top:0;left:0;right:0;bottom:0;pointer-events:none}
.bk-tick{position:absolute;top:0;bottom:0;width:2px;background:var(--trade);opacity:.75}
/* chart crosshair tooltip */
#xhair-tip{position:fixed;background:#1A1D24;border:1px solid var(--border);
  color:var(--text);font-size:10px;line-height:1.6;padding:4px 8px;border-radius:3px;
  pointer-events:none;display:none;z-index:1000;white-space:pre;
  font-family:'SF Mono','Roboto Mono',Consolas,monospace}
/* comparison panel */
#cmp-panel{flex:0 0 auto;background:var(--panel);border-top:1px solid var(--border);
  max-height:90px;overflow-y:auto}
.cmp-row{display:flex;justify-content:space-between;align-items:center;
  padding:2px 8px;border-bottom:1px solid #0D0F14}
.cmp-pos{color:var(--bid);font-size:10px;font-variant-numeric:tabular-nums}
.cmp-neg{color:var(--ask);font-size:10px;font-variant-numeric:tabular-nums}
</style>
</head>
<body>
)VHTML";

    html += std::string{ver_c} + "\n" + std::string{cnt_c} + "\n";

    // ── Body structure ────────────────────────────────────────────────────
    html += R"VHTML(<div class="app">
<header class="topbar">
  <span class="tb-title">MICROSTRUCTURE</span>
  <span class="sep">|</span>
  <div class="ctrl-grp">
    <button class="btn" onclick="goFirst()" title="First (Home)">&#171;&#171;</button>
    <button class="btn" onclick="goPrev()"  title="Prev (&#8592;)">&#8592;</button>
    <button class="btn" id="play-btn" onclick="togglePlay()" title="Play/Pause (Space)">&#9654;</button>
    <button class="btn" onclick="goNext()"  title="Next (&#8594;)">&#8594;</button>
    <button class="btn" onclick="goLast()"  title="Last (End)">&#187;&#187;</button>
  </div>
  <span class="sep">|</span>
  <span class="lbl">Speed:</span>
  <input type="range" class="sp-sl" id="speed-sl" min="50" max="2000" step="50" value="200"
         oninput="setSpeed(this.value)">
  <span id="spd">200ms</span>
  <span class="sep">|</span>
  <span class="lbl">Filter:</span>
  <select class="flt" id="ev-flt" onchange="setFilter(this.value)">
    <option value="ALL">All</option>
    <option value="TRADE">Trades</option>
    <option value="ADD">Adds</option>
    <option value="CANCEL">Cancels</option>
    <option value="MODIFY">Modifies</option>
  </select>
  <span class="sep">|</span>
  <button class="btn" id="hm-btn" onclick="toggleHeatmap()" title="Toggle liquidity heatmap">Heatmap</button>
  <span class="sep">|</span>
  <span class="lbl">Jump:</span>
  <input type="number" class="ji" id="jf" placeholder="frame" min="0"
         onkeydown="if(event.key==='Enter')jumpFrame()">
  <button class="btn" onclick="jumpFrame()">Go</button>
  <span class="sep">|</span>
  <span class="lbl">Event#:</span>
  <input type="number" class="ji" id="jeid" placeholder="id" min="0"
         onkeydown="if(event.key==='Enter')jumpEventId()" style="width:52px">
  <button class="btn" onclick="jumpEventId()">Go</button>
  <span class="sep">|</span>
  <button class="btn" id="bk-btn" onclick="toggleBookmark()" title="Bookmark (b)">&#9873;</button>
  <button class="btn" onclick="prevBookmark()" title="Prev bookmark ([)">[</button>
  <button class="btn" onclick="nextBookmark()" title="Next bookmark (])">]</button>
  <span class="sep">|</span>
  <button class="btn" id="cmp-btn" onclick="setCmp()" title="Set comparison anchor (c)">Cmp</button>
  <button class="btn" onclick="clearCmp()" title="Clear comparison (C)">&#215;</button>
  <span class="sep">|</span>
  <button class="btn" onclick="exportSnapshot()" title="Export frame JSON (e)">&#8595;JSON</button>
  <button class="btn" onclick="openSearch()" title="Search (/)">&#128269;</button>
  <span class="fi" id="fi">&#8212;</span>
</header>
<div id="srch-overlay">
  <div id="srch-box">
    <input id="srch-input" placeholder="event_id:#N  or  ts:NANOSECONDS  or  frame number"
           oninput="srchPreview(this.value)" onkeydown="srchKey(event)">
    <div id="srch-hint">Enter to jump &nbsp;&middot;&nbsp; Esc to close</div>
    <div id="srch-result"></div>
  </div>
</div>
<div id="xhair-tip"></div>
<div class="content">
  <div class="lcol">
    <div class="book-panel">
      <div class="phdr">
        <span class="ptitle">ORDER BOOK</span>
        <div id="tbadge"></div>
      </div>
      <div class="book-body">
        <div class="ask-side" id="ask-side"></div>
        <div class="spread-band" id="sband">&#8212;</div>
        <div class="bid-side" id="bid-side"></div>
      </div>
    </div>
    <div class="sigs-panel">
      <div class="phdr"><span class="ptitle">SIGNALS</span></div>
      <div id="sig-panel"></div>
    </div>
    <div id="cmp-panel">
      <div class="phdr">
        <span class="ptitle">COMPARISON</span>
        <span class="lbl" id="cmp-anchor"></span>
      </div>
      <div id="cmp-list"></div>
    </div>
    <div class="bk-panel" id="bk-panel">
      <div class="phdr">
        <span class="ptitle">BOOKMARKS</span>
        <span class="lbl" id="bk-count"></span>
      </div>
      <div id="bk-list"></div>
    </div>
  </div>
  <div class="rcol">
    <div class="charts-grid">
      <div class="chart-cell">
        <div class="chdr">
          <span class="ctitle">SPREAD</span>
          <span class="cval" id="cv-spread">&#8212;</span>
        </div>
        <canvas id="chart-spread"></canvas>
      </div>
      <div class="chart-cell">
        <div class="chdr">
          <span class="ctitle">IMBALANCE</span>
          <span class="cval" id="cv-imbalance">&#8212;</span>
        </div>
        <canvas id="chart-imbalance"></canvas>
      </div>
      <div class="chart-cell">
        <div class="chdr">
          <span class="ctitle">OFI</span>
          <span class="cval" id="cv-ofi">&#8212;</span>
        </div>
        <canvas id="chart-ofi"></canvas>
      </div>
      <div class="chart-cell">
        <div class="chdr">
          <span class="ctitle">MICROPRICE</span>
          <span class="cval" id="cv-microprice">&#8212;</span>
        </div>
        <canvas id="chart-microprice"></canvas>
      </div>
      <div class="chart-cell">
        <div class="chdr">
          <span class="ctitle">LIQ SLOPE</span>
          <span class="cval" id="cv-liqslope">&#8212;</span>
        </div>
        <canvas id="chart-liqslope"></canvas>
      </div>
      <div class="chart-cell">
        <div class="chdr">
          <span class="ctitle">CANCEL RATE</span>
          <span class="cval" id="cv-cancelrate">&#8212;</span>
        </div>
        <canvas id="chart-cancelrate"></canvas>
      </div>
    </div>
    <div class="hm-panel" id="hm-panel">
      <div class="phdr">
        <span class="ptitle">LIQUIDITY HEATMAP</span>
        <span class="lbl" id="hm-info"></span>
      </div>
      <canvas id="hm-canvas"></canvas>
    </div>
    <div class="tape" id="tape">
      <div class="phdr"><span class="ptitle">EVENT TAPE</span></div>
      <input id="tape-flt" placeholder="filter by type / id" oninput="renderTape()">
      <div id="tape-list"></div>
    </div>
  </div>
</div>
<div class="scr-bar">
  <div id="scr-wrap">
    <canvas id="scrubber"></canvas>
    <div id="bk-ticks"></div>
  </div>
  <div class="scr-lbl">
    <span>0</span>
    <span id="scr-mid">&#8212;</span>
    <span id="scr-max">&#8212;</span>
  </div>
</div>
</div>
<script>
const FRAMES=)VHTML";

    // ── Embedded JSON ─────────────────────────────────────────────────────
    html += frames_json;

    // ── JavaScript ────────────────────────────────────────────────────────
    html += R"VHTML(;
// ── state ──────────────────────────────────────────────────────────────────
var idx=0,playing=false,playTmr=null,speedMs=200,flt='ALL',showHm=false;
var bookmarks=[];
var xhairFrac=null;
var cmpIdx=-1;
var CHART_IDS=['chart-spread','chart-imbalance','chart-ofi',
               'chart-microprice','chart-liqslope','chart-cancelrate'];
var CHART_KEYS=['spread','imbalance','ofi','microprice','liquidity_slope','cancel_rate'];
var CHART_DPS=[3,4,4,4,4,4];

// ── bookmarks ──────────────────────────────────────────────────────────────
function loadBookmarks(){
  try{var s=localStorage.getItem('mse_bk');if(s)bookmarks=JSON.parse(s);}catch(ex){}
}
function saveBookmarks(){
  try{localStorage.setItem('mse_bk',JSON.stringify(bookmarks));}catch(ex){}
}
function toggleBookmark(){
  var i=bookmarks.indexOf(idx);
  if(i>=0)bookmarks.splice(i,1);
  else{bookmarks.push(idx);bookmarks.sort(function(a,b){return a-b;});}
  saveBookmarks();
  document.getElementById('bk-btn').className='btn'+(bookmarks.indexOf(idx)>=0?' active':'');
}
function prevBookmark(){
  stopPlay();
  for(var i=bookmarks.length-1;i>=0;i--){if(bookmarks[i]<idx){nav(bookmarks[i]);return;}}
}
function nextBookmark(){
  stopPlay();
  for(var i=0;i<bookmarks.length;i++){if(bookmarks[i]>idx){nav(bookmarks[i]);return;}}
}
function renderBookmarkPanel(){
  var btn=document.getElementById('bk-btn');
  if(btn)btn.className='btn'+(bookmarks.indexOf(idx)>=0?' active':'');
  var el=document.getElementById('bk-list');
  if(!el)return;
  var cnt=document.getElementById('bk-count');
  if(cnt)cnt.textContent=bookmarks.length?String(bookmarks.length):'';
  if(bookmarks.length===0){
    el.innerHTML='<div style="padding:3px 8px;color:var(--muted);font-size:10px">No bookmarks &mdash; press <b>b</b> to add</div>';
    return;
  }
  var h='';
  for(var i=0;i<bookmarks.length;i++){
    var bi=bookmarks[i],f=FRAMES[bi],isCur=(bi===idx);
    h+='<div class="bk-row'+(isCur?' bk-cur':'')+'" onclick="stopPlay();nav('+bi+')">'+
       '<div class="bk-dot"></div>'+
       '<span>Frame '+bi+'</span>'+
       '<span style="margin-left:auto;color:var(--muted)">'+f.event_type+' #'+f.event_id+'</span>'+
       '</div>';
  }
  el.innerHTML=h;
  // Tick marks on scrubber
  var bt=document.getElementById('bk-ticks');
  if(bt&&FRAMES.length>1){
    var th='';
    for(var j=0;j<bookmarks.length;j++){
      var pct=((bookmarks[j]/(FRAMES.length-1))*100).toFixed(3);
      th+='<div class="bk-tick" style="left:'+pct+'%"></div>';
    }
    bt.innerHTML=th;
  }
}

// ── jump to event id ────────────────────────────────────────────────────────
function jumpEventId(){
  stopPlay();
  var v=parseInt(document.getElementById('jeid').value,10);
  if(isNaN(v))return;
  for(var i=0;i<FRAMES.length;i++){
    if(FRAMES[i].event_id===v){nav(i);return;}
  }
  document.getElementById('fi').textContent='Event #'+v+' not found';
}

// ── search overlay ──────────────────────────────────────────────────────────
function openSearch(){
  var o=document.getElementById('srch-overlay');
  if(!o)return;
  o.classList.add('vis');
  var inp=document.getElementById('srch-input');
  if(inp){inp.value='';inp.focus();}
  document.getElementById('srch-result').textContent='';
}
function closeSearch(){
  var o=document.getElementById('srch-overlay');
  if(o)o.classList.remove('vis');
}
function srchResolve(q){
  q=(q||'').trim();
  if(!q)return null;
  var m;
  // event_id:#N or event_id:N
  if((m=q.match(/^(?:event_id:|#)(\d+)$/i))){
    var eid=parseInt(m[1],10);
    for(var i=0;i<FRAMES.length;i++){if(FRAMES[i].event_id===eid)return i;}
    return -1;
  }
  // ts:NANOSECONDS — compare as numeric strings to avoid float precision loss
  if((m=q.match(/^ts:(\d+)$/i))){
    var tsStr=m[1];
    var best=-1,bestDiff='';
    for(var j=0;j<FRAMES.length;j++){
      var fts=String(FRAMES[j].exchange_timestamp);
      var diff=numStrDiff(fts,tsStr);
      if(best<0||diff<bestDiff){best=j;bestDiff=diff;}
    }
    return best;
  }
  // Plain integer: frame index
  var n=parseInt(q,10);
  if(!isNaN(n)&&n>=0&&n<FRAMES.length)return n;
  return -1;
}
// Unsigned absolute difference of two non-negative decimal strings (as BigInt if available)
function numStrDiff(a,b){
  try{var ba=BigInt(a),bb=BigInt(b);return ba>bb?ba-bb:bb-ba;}catch(ex){}
  // Fallback: parse as float (loses last ~3 ns for large timestamps)
  return Math.abs(parseFloat(a)-parseFloat(b));
}
function srchPreview(q){
  var r=srchResolve(q);
  var el=document.getElementById('srch-result');
  if(!el)return;
  if(r===null){el.textContent='';return;}
  if(r<0){el.textContent='Not found';el.style.color='var(--ask)';return;}
  var f=FRAMES[r];
  el.style.color='var(--accent)';
  el.textContent='Frame '+r+' \u00b7 event_id='+f.event_id+' \u00b7 '+f.event_type;
}
function srchKey(e){
  if(e.key==='Escape'){closeSearch();return;}
  if(e.key==='Enter'){
    var r=srchResolve(document.getElementById('srch-input').value);
    if(r!==null&&r>=0){nav(r);closeSearch();}
  }
}

// ── frame snapshot export ────────────────────────────────────────────────────
function exportSnapshot(){
  var f=FRAMES[idx];
  var blob=new Blob([JSON.stringify(f,null,2)],{type:'application/json'});
  var url=URL.createObjectURL(blob);
  var a=document.createElement('a');
  a.href=url;a.download='frame_'+idx+'_ev'+f.event_id+'.json';
  document.body.appendChild(a);a.click();
  setTimeout(function(){document.body.removeChild(a);URL.revokeObjectURL(url);},800);
}

// ── before/after comparison ──────────────────────────────────────────────────
function setCmp(){
  cmpIdx=idx;
  var btn=document.getElementById('cmp-btn');
  if(btn)btn.className='btn active';
  render();
}
function clearCmp(){
  cmpIdx=-1;
  var btn=document.getElementById('cmp-btn');
  if(btn)btn.className='btn';
  render();
}
function renderCmp(){
  var anch=document.getElementById('cmp-anchor');
  var list=document.getElementById('cmp-list');
  if(!list)return;
  if(cmpIdx<0||cmpIdx===idx){
    if(anch)anch.textContent=cmpIdx<0?'':'At anchor';
    list.innerHTML='<div style="padding:3px 8px;color:var(--muted);font-size:10px">Press <b>c</b> to set anchor frame</div>';
    return;
  }
  if(anch)anch.textContent='vs Frame '+cmpIdx;
  var a=FRAMES[cmpIdx],b=FRAMES[idx];
  var fields=[
    ['\u0394Spread',    'spread',          3],
    ['\u0394Mid',       'mid',             2],
    ['\u0394Microprice','microprice',      4],
    ['\u0394Imbalance', 'imbalance',       6],
    ['\u0394OFI',       'ofi',             6],
    ['\u0394DepthRatio','depth_ratio',     4],
    ['\u0394CancelRate','cancel_rate',     6],
    ['\u0394LiqSlope',  'liquidity_slope', 6]
  ];
  var h='';
  for(var i=0;i<fields.length;i++){
    var lbl=fields[i][0],key=fields[i][1],dp=fields[i][2];
    var av=a[key],bv=b[key];
    if(av===null||bv===null||av===undefined||bv===undefined){
      h+='<div class="cmp-row"><span class="sl">'+lbl+'</span><span class="sv">\u2014</span></div>';
      continue;
    }
    var delta=bv-av;
    var cls=delta>0?'cmp-pos':delta<0?'cmp-neg':'sv';
    var sign=delta>0?'+':'';
    h+='<div class="cmp-row"><span class="sl">'+lbl+'</span>'+
       '<span class="'+cls+'">'+sign+delta.toFixed(dp)+'</span></div>';
  }
  list.innerHTML=h;
}

// ── utils ───────────────────────────────────────────────────────────────────
function fmt(v,dp){
  if(v===null||v===undefined)return'\u2014';
  if(typeof v==='number'){if(!isFinite(v))return'\u2014';return v.toFixed(dp);}
  return String(v);
}
function clamp(v,lo,hi){return Math.max(lo,Math.min(hi,v));}

// ── filtering ───────────────────────────────────────────────────────────────
function fltIdxs(){
  if(flt==='ALL')return null;
  var r=[];
  for(var i=0;i<FRAMES.length;i++){if(FRAMES[i].event_type===flt)r.push(i);}
  return r;
}

// ── navigation ──────────────────────────────────────────────────────────────
function nav(n){idx=clamp(n,0,FRAMES.length-1);render();}
function goFirst(){
  stopPlay();
  var fi=fltIdxs();
  nav(fi?fi[0]:0);
}
function goLast(){
  stopPlay();
  var fi=fltIdxs();
  nav(fi?fi[fi.length-1]:FRAMES.length-1);
}
function goNext(){
  stopPlay();
  var fi=fltIdxs();
  if(!fi){nav(idx+1);return;}
  for(var i=0;i<fi.length;i++){if(fi[i]>idx){nav(fi[i]);return;}}
}
function goPrev(){
  stopPlay();
  var fi=fltIdxs();
  if(!fi){nav(idx-1);return;}
  var p=-1;
  for(var i=0;i<fi.length;i++){if(fi[i]<idx)p=fi[i];else break;}
  if(p>=0)nav(p);
}
function jumpFrame(){
  stopPlay();
  var v=parseInt(document.getElementById('jf').value,10);
  if(!isNaN(v))nav(v);
}
function setSpeed(ms){
  speedMs=parseInt(ms,10);
  document.getElementById('spd').textContent=speedMs+'ms';
  if(playing){clearInterval(playTmr);playTmr=setInterval(advPlay,speedMs);}
}
function advPlay(){
  var fi=fltIdxs();
  if(!fi){
    if(idx<FRAMES.length-1){idx++;render();}else stopPlay();
  }else{
    for(var i=0;i<fi.length;i++){if(fi[i]>idx){idx=fi[i];render();return;}}
    stopPlay();
  }
}
function stopPlay(){
  if(!playing)return;
  playing=false;clearInterval(playTmr);
  document.getElementById('play-btn').innerHTML='&#9654;';
}
function togglePlay(){
  if(playing){stopPlay();}
  else{
    playing=true;
    document.getElementById('play-btn').innerHTML='&#9208;';
    playTmr=setInterval(advPlay,speedMs);
  }
}
function setFilter(v){flt=v;render();}
function toggleHeatmap(){
  showHm=!showHm;
  var p=document.getElementById('hm-panel');
  var b=document.getElementById('hm-btn');
  if(showHm){p.classList.add('vis');b.classList.add('active');}
  else{p.classList.remove('vis');b.classList.remove('active');}
  render();
}

// ── render: frame info ────────────────────────────────────────────────────────
function renderInfo(){
  var f=FRAMES[idx];
  document.getElementById('fi').textContent=
    'Frame '+idx+' / '+(FRAMES.length-1)+
    '  \u00b7  #'+f.event_id+'  \u00b7  '+f.event_type+'  \u00b7  '+f.venue;
}

// ── render: trade badge ───────────────────────────────────────────────────────
function renderBadge(){
  var f=FRAMES[idx];
  var el=document.getElementById('tbadge');
  if(f.is_trade&&f.trade){
    var t=f.trade;
    el.innerHTML='<span class="tbadge">TRADE @ '+t.price+
      ' \u00d7'+t.size+' \u00b7 '+f.last_trade_aggressor+'</span>';
  }else{el.innerHTML='';}
}

// ── render: order book ────────────────────────────────────────────────────────
function renderBook(){
  var f=FRAMES[idx];
  var asks=f.ask_levels||[];
  var bids=f.bid_levels||[];
  var allV=asks.concat(bids).map(function(l){return l.volume;});
  var maxV=allV.length?Math.max.apply(null,allV):1;
  var tradePx=(f.is_trade&&f.trade)?f.trade.price:null;

  function lvl(levels,side){
    return levels.map(function(l){
      var pct=Math.max(0.5,(l.volume/maxV)*100).toFixed(1);
      var isTrade=(tradePx!==null&&l.price===tradePx);
      var cls='lr '+side+'-l'+(isTrade?' trade-level':'');
      var ex='flow:'+fmt(l.order_flow,4)+
        ' \u00b7 cancel:'+fmt(l.cancel_rate,4)+
        ' \u00b7 fill:'+fmt(l.fill_rate,4);
      return '<div class="'+cls+'">'+
        '<div class="lbar" style="width:'+pct+'%"></div>'+
        '<span class="lp">'+l.price+'</span>'+
        '<span class="lv">'+l.volume+'</span>'+
        '<span class="lq">q'+l.queue_depth+'</span>'+
        '<span class="lx">'+ex+'</span></div>';
    }).join('');
  }

  document.getElementById('ask-side').innerHTML=lvl(asks.slice().reverse(),'ask');
  var sb=document.getElementById('sband');
  if(f.best_bid!==null&&f.best_ask!==null){
    sb.textContent='spread '+fmt(f.spread,2)+
      '  \u00b7  mid '+fmt(f.mid,2)+
      '  \u00b7  \u03bcP '+fmt(f.microprice,4);
  }else{sb.textContent='\u2014 one-sided \u2014';}
  document.getElementById('bid-side').innerHTML=lvl(bids,'bid');
}

// ── render: signals ───────────────────────────────────────────────────────────
function renderSigs(){
  var f=FRAMES[idx];
  var rc='regime-'+(f.regime||'illiquid');
  var rows=[
    ['Spread',     fmt(f.spread,4)+' tk'],
    ['Mid',        fmt(f.mid,2)],
    ['Microprice', fmt(f.microprice,4)],
    ['Imbalance',  fmt(f.imbalance,6)],
    ['OFI',        fmt(f.ofi,6)],
    ['Depth ratio',fmt(f.depth_ratio,4)],
    ['Cancel rate',fmt(f.cancel_rate,6)],
    ['Queue \u00bd-life',fmt(f.queue_half_life,4)],
    ['Liq slope',  fmt(f.liquidity_slope,6)],
    ['Net latency',fmt(f.network_latency,0)+' ns'],
    ['Gwy latency',fmt(f.gateway_latency,0)+' ns']
  ];
  var h='';
  for(var i=0;i<rows.length;i++){
    h+='<div class="sr"><span class="sl">'+rows[i][0]+'</span>'+
       '<span class="sv">'+rows[i][1]+'</span></div>';
  }
  h+='<div class="sr"><span class="sl">Regime</span>'+
     '<span class="sv '+rc+'">'+(f.regime||'\u2014')+'</span></div>';
  h+='<div class="sr"><span class="sl">Aggressor</span>'+
     '<span class="sv">'+(f.last_trade_aggressor||'\u2014')+'</span></div>';
  document.getElementById('sig-panel').innerHTML=h;
}

// ── render: time-series charts ────────────────────────────────────────────────
var CWIN=200;

function drawChart(cid,key,color,dp){
  var canvas=document.getElementById(cid);
  if(!canvas)return false;
  var W=canvas.clientWidth||canvas.offsetWidth;
  var H=canvas.clientHeight||canvas.offsetHeight;
  if(W<4||H<4)return false;
  canvas.width=W;canvas.height=H;
  var ctx=canvas.getContext('2d');

  var start=Math.max(0,idx-CWIN+1);
  var vals=[];
  for(var i=start;i<=idx;i++){
    var v=FRAMES[i][key];
    vals.push((v!==null&&v!==undefined&&isFinite(v))?v:null);
  }

  var cvId='cv-'+cid.replace('chart-','');
  var cur=vals[vals.length-1];
  var cvEl=document.getElementById(cvId);
  if(cvEl)cvEl.textContent=cur!==null?cur.toFixed(dp):'\u2014';

  ctx.fillStyle='#0D0F14';ctx.fillRect(0,0,W,H);

  var valid=vals.filter(function(v){return v!==null;});
  if(valid.length<2)return true;
  var lo=Math.min.apply(null,valid);
  var hi=Math.max.apply(null,valid);
  var rng=hi-lo||1;
  var pL=2,pR=2,pT=11,pB=11;
  function toX(i){return pL+(W-pL-pR)*i/(vals.length-1);}
  function toY(v){return v===null?null:pT+(H-pT-pB)*(1-(v-lo)/rng);}

  // grid lines
  ctx.strokeStyle='#1E2028';ctx.lineWidth=1;
  for(var g=1;g<4;g++){
    var gy=pT+(H-pT-pB)*g/4;
    ctx.beginPath();ctx.moveTo(0,gy);ctx.lineTo(W,gy);ctx.stroke();
  }

  // trade markers — amber vertical dashes at trade frames
  ctx.fillStyle='rgba(245,158,11,.18)';
  for(var ti=0;ti<vals.length;ti++){
    if(FRAMES[start+ti]&&FRAMES[start+ti].is_trade){
      var tx=toX(ti);ctx.fillRect(tx-0.5,pT,1,H-pT-pB);
    }
  }

  // zero line for oscillating signals
  if(key==='imbalance'||key==='ofi'){
    var zy=toY(0);
    if(zy!==null&&zy>pT&&zy<H-pB){
      ctx.strokeStyle='#FFFFFF15';ctx.lineWidth=1;
      ctx.setLineDash([3,3]);
      ctx.beginPath();ctx.moveTo(0,zy);ctx.lineTo(W,zy);ctx.stroke();
      ctx.setLineDash([]);
    }
  }

  // y-axis labels
  if(H>36){
    var ldp=dp>3?3:dp;
    ctx.fillStyle='#3A3E4A';ctx.font='8px monospace';ctx.textAlign='left';
    ctx.fillText(hi.toFixed(ldp),3,8);
    ctx.fillText(lo.toFixed(ldp),3,H-2);
  }

  // data line
  ctx.strokeStyle=color;ctx.lineWidth=1.5;
  ctx.beginPath();var started=false;
  for(var di=0;di<vals.length;di++){
    var dy=toY(vals[di]);
    if(dy===null){started=false;continue;}
    if(!started){ctx.moveTo(toX(di),dy);started=true;}
    else ctx.lineTo(toX(di),dy);
  }
  ctx.stroke();

  // current value dot
  if(cur!==null){
    var cx=toX(vals.length-1),cy=toY(cur);
    if(cy!==null){
      ctx.beginPath();ctx.arc(cx,cy,2.5,0,Math.PI*2);
      ctx.fillStyle=color;ctx.fill();
    }
  }

  // crosshair overlay
  if(xhairFrac!==null){
    var xhx=pL+(W-pL-pR)*xhairFrac;
    ctx.strokeStyle='rgba(255,255,255,.22)';ctx.lineWidth=1;ctx.setLineDash([3,3]);
    ctx.beginPath();ctx.moveTo(xhx,pT);ctx.lineTo(xhx,H-pB);ctx.stroke();
    ctx.setLineDash([]);
    var xhi=Math.round(xhairFrac*(vals.length-1));
    var xhv=vals[xhi];
    if(xhv!==null){
      var xhy=toY(xhv);
      ctx.beginPath();ctx.arc(xhx,xhy,2.5,0,Math.PI*2);
      ctx.fillStyle='rgba(255,255,255,.55)';ctx.fill();
    }
  }
  return true;
}

function renderCharts(){
  var ok=drawChart('chart-spread',    'spread',          '#4DA3FF',3);
  drawChart('chart-imbalance', 'imbalance',       '#F59E0B',4);
  drawChart('chart-ofi',       'ofi',             '#A78BFA',4);
  drawChart('chart-microprice','microprice',      '#2ECC71',4);
  drawChart('chart-liqslope',  'liquidity_slope', '#FF6B9D',4);
  drawChart('chart-cancelrate','cancel_rate',     '#6CB8FF',4);
  if(!ok)setTimeout(render,60);
}

// ── chart crosshair hover ──────────────────────────────────────────────────
(function(){
  function onMove(e){
    var canvas=e.currentTarget;
    var rect=canvas.getBoundingClientRect();
    xhairFrac=clamp((e.clientX-rect.left)/rect.width,0,1);
    // Build tooltip: frame + all signal values at hover position
    var start=Math.max(0,idx-CWIN+1);
    var nF=idx-start+1;
    var hfi=start+Math.round(xhairFrac*(nF-1));
    hfi=clamp(hfi,0,FRAMES.length-1);
    var f=FRAMES[hfi];
    if(!f){return;}
    var lines='Frame '+hfi+' \u00b7 ev#'+f.event_id+' \u00b7 '+f.event_type;
    for(var ci=0;ci<CHART_KEYS.length;ci++){
      var v=f[CHART_KEYS[ci]];
      if(v!==null&&v!==undefined&&isFinite(v)){
        lines+='\n'+CHART_KEYS[ci]+': '+v.toFixed(CHART_DPS[ci]);
      }
    }
    var tip=document.getElementById('xhair-tip');
    if(tip){
      tip.style.display='block';
      tip.style.left=(e.clientX+14)+'px';
      tip.style.top=(e.clientY-6)+'px';
      tip.textContent=lines;
    }
    renderCharts();
  }
  function onLeave(){
    xhairFrac=null;
    var tip=document.getElementById('xhair-tip');
    if(tip)tip.style.display='none';
    renderCharts();
  }
  for(var ci=0;ci<CHART_IDS.length;ci++){
    var el=document.getElementById(CHART_IDS[ci]);
    if(el){
      el.addEventListener('mousemove',onMove);
      el.addEventListener('mouseleave',onLeave);
    }
  }
})();

// ── render: liquidity heatmap ─────────────────────────────────────────────────
// X axis = time (frames in window), Y axis = price, colour = bid/ask volume.
// Trade executions shown as amber dots.
function drawHeatmap(){
  var canvas=document.getElementById('hm-canvas');
  if(!canvas)return;
  var W=canvas.clientWidth||canvas.offsetWidth;
  var H=canvas.clientHeight||canvas.offsetHeight;
  if(W<4||H<8)return;
  canvas.width=W;canvas.height=H;
  var ctx=canvas.getContext('2d');
  ctx.fillStyle='#0D0F14';ctx.fillRect(0,0,W,H);

  var start=Math.max(0,idx-CWIN+1);
  var nF=idx-start+1;
  if(nF<2)return;

  // Pass 1: find price range and max volume
  var minP=Infinity,maxP=-Infinity,maxVol=1;
  for(var i=start;i<=idx;i++){
    var f=FRAMES[i];
    var lvls=(f.bid_levels||[]).concat(f.ask_levels||[]);
    for(var j=0;j<lvls.length;j++){
      if(lvls[j].price<minP)minP=lvls[j].price;
      if(lvls[j].price>maxP)maxP=lvls[j].price;
      if(lvls[j].volume>maxVol)maxVol=lvls[j].volume;
    }
  }
  if(minP===Infinity)return;
  var priceRange=maxP-minP||1;

  // Column width (each frame = one column)
  var colW=Math.max(1,(W-1)/(nF>1?nF-1:1));
  // Estimate tick height: render rectangles 2px tall (price granularity)
  var lvlH=Math.max(2,Math.min(8,H/Math.max(1,priceRange)*2));

  function toX(fi){return (nF>1)?((fi-start)/(nF-1))*(W-colW):0;}
  function toY(p){return H-1-((p-minP)/priceRange)*(H-1);}

  // Pass 2: draw levels
  for(var fi=start;fi<=idx;fi++){
    var frame=FRAMES[fi];
    var x=toX(fi);

    var bids=frame.bid_levels||[];
    for(var bi=0;bi<bids.length;bi++){
      var l=bids[bi];
      var alpha=Math.min(0.85,(l.volume/maxVol)*1.6);
      var y=toY(l.price);
      ctx.fillStyle='rgba(46,204,113,'+alpha.toFixed(3)+')';
      ctx.fillRect(x,Math.max(0,y-lvlH*0.5),colW+0.5,lvlH);
    }
    var asks=frame.ask_levels||[];
    for(var ai=0;ai<asks.length;ai++){
      var la=asks[ai];
      var alphaa=Math.min(0.85,(la.volume/maxVol)*1.6);
      var ya=toY(la.price);
      ctx.fillStyle='rgba(255,92,92,'+alphaa.toFixed(3)+')';
      ctx.fillRect(x,Math.max(0,ya-lvlH*0.5),colW+0.5,lvlH);
    }

    // Mid price faint line
    if(frame.best_bid!==null&&frame.best_ask!==null){
      var midP=(frame.best_bid+frame.best_ask)/2.0;
      var ym=toY(midP);
      ctx.fillStyle='rgba(230,180,80,.25)';
      ctx.fillRect(x,Math.max(0,ym-0.5),colW+0.5,1);
    }
  }

  // Pass 3: trade markers (amber dots on top)
  for(var fi2=start;fi2<=idx;fi2++){
    var frame2=FRAMES[fi2];
    if(frame2.is_trade&&frame2.trade){
      var xt=toX(fi2)+colW*0.5;
      var yt=toY(frame2.trade.price);
      ctx.beginPath();
      ctx.arc(xt,yt,Math.min(3,colW*0.6+1),0,Math.PI*2);
      ctx.fillStyle='rgba(245,158,11,.95)';
      ctx.fill();
    }
  }

  // Current frame cursor
  var cxh=toX(idx)+colW*0.5;
  ctx.strokeStyle='#FFFFFF55';ctx.lineWidth=1;
  ctx.beginPath();ctx.moveTo(cxh,0);ctx.lineTo(cxh,H);ctx.stroke();

  // Price axis labels (right edge)
  ctx.fillStyle='#3A3E4A';ctx.font='8px monospace';ctx.textAlign='right';
  ctx.fillText(String(maxP),W-2,9);
  ctx.fillText(String(minP),W-2,H-2);

  // Info: price range and frame window
  var hmInfo=document.getElementById('hm-info');
  if(hmInfo)hmInfo.textContent=
    'px '+minP+'\u2013'+maxP+'  \u00b7  '+nF+' frames';
}

// ── render: event tape ────────────────────────────────────────────────────────
function renderTape(){
  var tfEl=document.getElementById('tape-flt');
  var tfVal=tfEl?tfEl.value.trim().toLowerCase():'';
  var start=Math.max(0,idx-39);
  var rows='';
  for(var i=start;i<=idx;i++){
    var f=FRAMES[i];
    // event-type filter (same filter that governs Next/Prev/Play)
    if(flt!=='ALL'&&f.event_type!==flt)continue;
    // tape text search filter
    if(tfVal){
      var hay=(String(f.event_id)+' '+f.event_type).toLowerCase();
      if(hay.indexOf(tfVal)<0)continue;
    }
    var isCur=(i===idx);
    var isBm=(bookmarks.indexOf(i)>=0);
    var px=f.is_trade&&f.trade?f.trade.price:
      (f.best_bid!==null?f.best_bid:(f.best_ask!==null?f.best_ask:'\u2014'));
    rows+='<div class="tape-row'+(isCur?' cur':'')+'" onclick="nav('+i+')">'+
      '<span class="tet et-'+f.event_type+'">'+f.event_type+'</span>'+
      '<span class="tep">'+px+'</span>'+
      (isBm?'<span style="color:var(--trade);font-size:9px">\u2605</span>':'')+
      '<span class="teid">#'+f.event_id+'</span>'+
      '</div>';
  }
  var tl=document.getElementById('tape-list');
  tl.innerHTML=rows;
  var tp=document.getElementById('tape');
  tp.scrollTop=tp.scrollHeight;
}

// ── render: timeline scrubber ─────────────────────────────────────────────────
// Scrubber shows: regime colouring, trade density (bottom band), cursor, progress.
function renderScrubber(){
  var canvas=document.getElementById('scrubber');
  var W=canvas.clientWidth||canvas.offsetWidth||400;
  canvas.width=W;canvas.height=26;
  var ctx=canvas.getContext('2d');
  var H=26,n=FRAMES.length;
  ctx.fillStyle='#0A0B0E';ctx.fillRect(0,0,W,H);
  if(n<2)return;

  // Build filter membership set (O(1) lookup per frame in bucket pass)
  var fltSet=null;
  if(flt!=='ALL'){
    fltSet={};
    for(var fs=0;fs<FRAMES.length;fs++){
      if(FRAMES[fs].event_type===flt)fltSet[fs]=1;
    }
  }

  // bucket pass — one canvas-column = one bucket
  var bucks=Math.min(W,n);var bw=W/bucks;
  for(var b=0;b<bucks;b++){
    var bf=Math.floor(b*n/bucks);
    var be=Math.min(n,Math.floor((b+1)*n/bucks)+1);
    var hasTrade=false,regime='normal',hasMatch=!fltSet;
    for(var i=bf;i<be;i++){
      if(FRAMES[i].is_trade)hasTrade=true;
      regime=FRAMES[i].regime||'normal';
      if(fltSet&&fltSet[i])hasMatch=true;
    }
    var x=b*bw;
    if(regime==='stressed'){ctx.fillStyle='rgba(255,92,92,.07)';ctx.fillRect(x,0,bw+.5,H);}
    else if(regime==='tight'){ctx.fillStyle='rgba(46,204,113,.05)';ctx.fillRect(x,0,bw+.5,H);}
    // trade density bar in bottom 40%
    if(hasTrade){ctx.fillStyle='rgba(245,158,11,.4)';ctx.fillRect(x,H*.6,bw+.5,H*.4);}
    // dim non-matching buckets when filter is active
    if(fltSet&&!hasMatch){ctx.fillStyle='rgba(0,0,0,.55)';ctx.fillRect(x,0,bw+.5,H);}
  }

  // playhead fill + cursor
  var px=(idx/(n-1))*W;
  ctx.fillStyle='rgba(77,163,255,.08)';ctx.fillRect(0,0,px,H);
  ctx.fillStyle='#4DA3FF';ctx.fillRect(Math.round(px)-1,0,2,H);

  document.getElementById('scr-mid').textContent='Frame '+idx+' / '+(n-1);
  document.getElementById('scr-max').textContent=String(n-1);
}

// ── main render ───────────────────────────────────────────────────────────────
function render(){
  if(!FRAMES||FRAMES.length===0){
    document.getElementById('fi').textContent='No frames loaded.';return;
  }
  renderInfo();renderBadge();renderBook();renderSigs();renderCmp();
  renderCharts();
  if(showHm)drawHeatmap();
  renderTape();renderScrubber();renderBookmarkPanel();
}

// ── scrubber click/drag ───────────────────────────────────────────────────────
function scrubSeek(clientX){
  var canvas=document.getElementById('scrubber');
  var rect=canvas.getBoundingClientRect();
  var frac=clamp((clientX-rect.left)/rect.width,0,1);
  stopPlay();nav(Math.round(frac*(FRAMES.length-1)));
}
var scrubbing=false;
var scr=document.getElementById('scrubber');
scr.addEventListener('mousedown',function(e){scrubbing=true;scrubSeek(e.clientX);});
document.addEventListener('mousemove',function(e){if(scrubbing)scrubSeek(e.clientX);});
document.addEventListener('mouseup',function(){scrubbing=false;});
scr.addEventListener('touchstart',function(e){
  e.preventDefault();scrubSeek(e.touches[0].clientX);},{passive:false});
scr.addEventListener('touchmove',function(e){
  e.preventDefault();scrubSeek(e.touches[0].clientX);},{passive:false});

// ── keyboard shortcuts ────────────────────────────────────────────────────────
document.addEventListener('keydown',function(e){
  var tag=e.target.tagName;
  if(tag==='INPUT'||tag==='SELECT')return;
  if(e.key==='ArrowRight'||e.key==='n')goNext();
  else if(e.key==='ArrowLeft'||e.key==='p')goPrev();
  else if(e.key===' '){e.preventDefault();togglePlay();}
  else if(e.key==='Home')goFirst();
  else if(e.key==='End')goLast();
  else if(e.key==='h')toggleHeatmap();
  else if(e.key==='b')toggleBookmark();
  else if(e.key==='[')prevBookmark();
  else if(e.key===']')nextBookmark();
  else if(e.key==='e')exportSnapshot();
  else if(e.key==='c')setCmp();
  else if(e.key==='C')clearCmp();
  else if(e.key==='/')   {e.preventDefault();openSearch();}
});
document.addEventListener('keydown',function(e){
  if(e.key==='Escape'){closeSearch();}
});

window.addEventListener('resize',render);

// ── init ──────────────────────────────────────────────────────────────────────
loadBookmarks();
requestAnimationFrame(render);
</script>
</body>
</html>
)VHTML";

    return html;
}

// ── Public API ────────────────────────────────────────────────────────────────

std::string HtmlExporter::render_html(
    const std::vector<VisualizationFrame>& frames) const
{
    JsonSerializer ser;
    const std::string frames_json = ser.serialize_frames(frames);
    return build_html(frames_json, frames.size());
}

bool HtmlExporter::write_html(const std::vector<VisualizationFrame>& frames,
                               const std::string& output_path) const
{
    const std::string html = render_html(frames);
    std::ofstream out{output_path, std::ios::binary};
    if (!out.is_open()) return false;
    out << html;
    return out.good();
}

} // namespace visualization
