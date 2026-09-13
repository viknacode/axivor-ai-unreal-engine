// Copyright 2026, BlueprintsLab, All rights reserved

#include "Utils/DiagramUtils.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "SWebBrowser.h"
#include "IWebBrowserWindow.h"
#include "Misc/Base64.h"
#include "Editor.h"

DEFINE_LOG_CATEGORY_STATIC(LogDiagramUtils, Log, All);

namespace DiagramUtils
{
	FString CreateDiagramPopoutHtml(const FString& DiagramData)
	{
		return FString::Printf(TEXT(
			"<!DOCTYPE html><html><head><style>"
			"body{background:#1e1e1e;margin:0;font-family:'Segoe UI',Arial,sans-serif;overflow:hidden;height:100vh;}"
			"#toolbar{position:fixed;top:10px;left:10px;display:flex;gap:8px;align-items:center;background:#2d2d30;padding:8px 12px;border-radius:4px;border:1px solid #404040;z-index:100;}"
			"#toolbar span{color:#888;font-size:11px;}"
			"#toolbar kbd{background:#444;color:#ccc;padding:2px 6px;border-radius:3px;font-size:10px;border:1px solid #555;}"
			"#zoom-pct{color:#b0b0b0;font-size:12px;min-width:40px;text-align:center;border-left:1px solid #404040;padding-left:8px;}"
			"button{background:#444;color:#d4d4d4;border:none;padding:4px 10px;border-radius:3px;cursor:pointer;font-size:11px;}"
			"button:hover{background:#555;}"
			"#container{position:absolute;transform-origin:0 0;cursor:grab;width:100%%;height:100%%;display:flex;justify-content:center;align-items:center;}"
			"#container.dragging{cursor:grabbing;}"
			"#container svg{max-width:95vw;max-height:90vh;}"
			"</style></head><body>"
			"<div id='toolbar'>"
			"<span><kbd>Ctrl</kbd>+Scroll zoom | <kbd>Drag</kbd> pan</span>"
			"<span id='zoom-pct'>100%%</span>"
			"<button onclick='resetView()'>Reset</button>"
			"</div>"
			"<div id='container'>%s</div>"
			"<script>"
			"var zoom=100,panX=0,panY=0,isDragging=false,startX,startY;"
			"var container=document.getElementById('container');"
			"function updateView(){container.style.transform='translate('+panX+'px,'+panY+'px) scale('+zoom/100+')';document.getElementById('zoom-pct').textContent=zoom+'%%';}"
			"function resetView(){zoom=100;panX=0;panY=0;updateView();}"
			"document.addEventListener('wheel',function(e){if(e.ctrlKey){e.preventDefault();zoom+=e.deltaY>0?-10:10;zoom=Math.max(20,Math.min(500,zoom));updateView();}},{passive:false});"
			"container.addEventListener('mousedown',function(e){if(e.button===0){isDragging=true;container.classList.add('dragging');startX=e.clientX-panX;startY=e.clientY-panY;e.preventDefault();}});"
			"document.addEventListener('mousemove',function(e){if(isDragging){panX=e.clientX-startX;panY=e.clientY-startY;updateView();}});"
			"document.addEventListener('mouseup',function(){isDragging=false;container.classList.remove('dragging');});"
			"</script></body></html>"
		), *DiagramData);
	}

	TSharedPtr<SWindow> CreateDiagramWindow(const FString& DiagramData)
	{
		FString DiagramHtml = CreateDiagramPopoutHtml(DiagramData);

		TSharedRef<SWindow> DiagramWindow = SNew(SWindow)
			.Title(FText::FromString(TEXT("Diagram Viewer")))
			.ClientSize(FVector2D(1024, 768))
			.SupportsMaximize(true)
			.SupportsMinimize(true)
			.AutoCenter(EAutoCenter::PreferredWorkArea);

		TSharedPtr<SWebBrowser> DiagramBrowser;
		DiagramWindow->SetContent(
			SAssignNew(DiagramBrowser, SWebBrowser)
			.SupportsTransparency(false)
			.ShowControls(false)
			.ShowAddressBar(false)
		);

		if (DiagramBrowser.IsValid() && GEditor)
		{
			TArray<uint8> HtmlBytes;
			FTCHARToUTF8 Conv(*DiagramHtml);
			HtmlBytes.Append((const uint8*)Conv.Get(), Conv.Length());
			FString DataUri = TEXT("data:text/html;base64,") + FBase64::Encode(HtmlBytes);

			TWeakPtr<SWebBrowser> WeakBrowser = DiagramBrowser;
			TSharedPtr<FString> UriCopy = MakeShared<FString>(DataUri);
			FTimerHandle DiagramLoadTimer;
			GEditor->GetTimerManager()->SetTimer(DiagramLoadTimer, [WeakBrowser, UriCopy]()
			{
				if (auto B = WeakBrowser.Pin())
					B->LoadURL(*UriCopy);
			}, 0.3f, false);
		}

		return DiagramWindow;
	}

	FString GetDiagramCloseResponseHtml()
	{
		return TEXT(
			"<!DOCTYPE html><html><head><script>"
			"if(typeof closeModal==='function')closeModal();"
			"setTimeout(function(){history.back();},10);"
			"</script></head><body style='background:#1e1e1e'></body></html>"
		);
	}

	TSharedPtr<SWindow> CreateProjectDashboardWindow(
		const FString& DepGraphJson,
		const FString& HeatmapData,
		const FString& PieChartData,
		const FString& InheritanceNomnoml,
		const FString& PerformanceJson,
		const FString& PastTracesJson,
		const FString& VisNetworkJs,
		const FString& NomnomlJs,
		const FString& GraphreJs,
		FOnDashboardLoadUrl OnLoadUrl)
	{
		FString Html;
		Html.Reserve(VisNetworkJs.Len() + NomnomlJs.Len() + GraphreJs.Len() + 20000);
		Html += TEXT("<!DOCTYPE html><html><head><meta charset='utf-8'><script>");
		Html += VisNetworkJs;
		Html += TEXT("</script><script>");
		Html += GraphreJs;
		Html += TEXT("</script><script>");
		Html += NomnomlJs;
		Html += TEXT("</script><style>"
			"*{box-sizing:border-box;margin:0;padding:0;}"
			"body{font:14px 'Segoe UI',Arial;background:#1e1e1e;color:#d4d4d4;height:100vh;overflow:hidden;}"
			".tabs{display:flex;background:#2d2d30;border-bottom:2px solid #007acc;}"
			".tab{padding:10px 20px;cursor:pointer;color:#9d9d9d;border-bottom:2px solid transparent;transition:all 0.2s;}"
			".tab:hover{color:#d4d4d4;background:#363636;}"
			".tab.active{color:#fff;border-bottom-color:#007acc;background:#1e1e1e;}"
			".panel{display:none;height:calc(100vh - 44px);overflow:auto;}"
			".panel.active{display:block;}"
			"#depgraph-container{width:100%;height:100%;}"
			"#heatmap-container,#pie-container{padding:20px;overflow:auto;}"
			"#inheritance-container{padding:20px;overflow:auto;}"
			".zoomable{transform-origin:0 0;cursor:grab;}"
			".zoomable.dragging{cursor:grabbing;}"
			"h2{color:#e0e0e0;margin:0 0 16px 0;font-size:18px;}"
			".legend{display:flex;gap:16px;justify-content:center;margin:12px 0;font-size:12px;}"
			".legend span{display:flex;align-items:center;gap:4px;}"
			".legend .dot{width:12px;height:12px;border-radius:2px;}"
			"</style></head><body>"
			"<div class='tabs'>"
			"<div class='tab active' onclick='showTab(0)'>Dependency Graph</div>"
			"<div class='tab' onclick='showTab(1)'>Complexity Heatmap</div>"
			"<div class='tab' onclick='showTab(2)'>Asset Distribution</div>"
			"<div class='tab' onclick='showTab(3)'>Inheritance Tree</div>"
			"<div class='tab' onclick='showTab(4)'>Performance</div>"
			"</div>"
			"<div class='panel active' id='panel-0'><div id='depgraph-container'></div></div>"
			"<div class='panel' id='panel-1'><div id='heatmap-container'><h2>Blueprint Complexity Heatmap</h2><div id='heatmap-svg'></div></div></div>"
			"<div class='panel' id='panel-2'><div id='pie-container'><h2>Asset Distribution</h2><div id='pie-svg'></div></div></div>"
			"<div class='panel' id='panel-3'><div id='inheritance-container'><h2>Inheritance Tree</h2><div id='inheritance-svg'></div></div></div>"
			"<div class='panel' id='panel-4'><div id='perf-container' style='padding:20px;'><div style='display:flex;align-items:center;gap:12px;margin-bottom:12px;'><h2 style='margin:0;'>Performance Profile</h2>"
			"<div id='trace-selector' style='position:relative;display:inline-block;background:#2d2d30;color:#d4d4d4;border:1px solid #404040;border-radius:4px;padding:4px 12px;font-size:12px;cursor:pointer;min-width:200px;user-select:none;' onclick='ddTrToggle(event)'>"
			"<span id='trace-selector-label'>Current trace</span>"
			"<span style='float:right;margin-left:8px;color:#888;'>&#9662;</span>"
			"<div id='trace-selector-options' style='display:none;position:absolute;top:calc(100% + 2px);left:0;right:0;background:#2d2d30;border:1px solid #404040;border-radius:4px;max-height:240px;overflow:auto;z-index:1000;text-align:left;box-shadow:0 4px 12px rgba(0,0,0,0.4);'>"
			"<div class='dd-opt' style='padding:6px 12px;cursor:pointer;' onclick='ddTrPick(event,\"\",\"Current trace\")' onmouseover='this.style.background=\"#404040\"' onmouseout='this.style.background=\"\"'>Current trace</div>"
			"</div>"
			"</div>"
			"</div><div id='perf-content'><p style='color:#888;'>Click \"Profile Project\" in the Project Scanner to capture performance data.</p></div></div></div>"
			"<script>"
			"window.onerror=function(msg,url,line){document.body.innerHTML='<div style=\"padding:40px;text-align:center;\"><h2 style=\"color:#ffa07a;\">Dashboard Error</h2><p>'+msg+'</p><p style=\"color:#888;font-size:12px;\">This usually means a required JavaScript library failed to load. Try reopening the dashboard or reinstalling the plugin.</p></div>';return true;};"
			"function showTab(i){document.querySelectorAll('.tab').forEach(function(t,j){t.classList.toggle('active',j===i);});document.querySelectorAll('.panel').forEach(function(p,j){p.classList.toggle('active',j===i);});}"
			"function loadTrace(path){if(path){document.title='Loading: '+path;window.location.href='ue://analyze-trace?path='+encodeURIComponent(path);}}"
			"function ddTrToggle(ev){if(ev)ev.stopPropagation();var o=document.getElementById('trace-selector-options');o.style.display=(o.style.display==='block')?'none':'block';}"
			"function ddTrPick(ev,val,lbl){if(ev)ev.stopPropagation();document.getElementById('trace-selector-label').textContent=lbl;document.getElementById('trace-selector-options').style.display='none';if(val)loadTrace(val);}"
			"document.addEventListener('click',function(ev){var s=document.getElementById('trace-selector');if(s&&!s.contains(ev.target)){var o=document.getElementById('trace-selector-options');if(o)o.style.display='none';}});"
		);

		Html += TEXT("var pastTraces=");
		Html += PastTracesJson;
		Html += TEXT(";"
			"if(pastTraces&&pastTraces.length>0){"
			"var optsEl=document.getElementById('trace-selector-options');"
			"pastTraces.forEach(function(t){"
			"var d=document.createElement('div');"
			"d.className='dd-opt';"
			"d.style.cssText='padding:6px 12px;cursor:pointer;';"
			"d.onmouseover=function(){this.style.background='#404040';};"
			"d.onmouseout=function(){this.style.background='';};"
			"var lbl=t.name+' ('+t.size_mb.toFixed(1)+' MB)';"
			"d.textContent=lbl;"
			"d.onclick=(function(p,l){return function(ev){ddTrPick(ev,p,l);};})(t.path,lbl);"
			"optsEl.appendChild(d);"
			"});}"
		);

		Html += TEXT("var depData=");
		Html += DepGraphJson.IsEmpty() ? TEXT("{\"nodes\":[],\"edges\":[]}") : *DepGraphJson;
		Html += TEXT(";"
			"var typeColors={'actor':'#4fc3f7','character':'#81c784','pawn':'#a5d6a7','widget':'#ffb74d','anim_bp':'#f06292','material':'#ba68c8','niagara':'#4db6ac','behavior_tree':'#ffd54f','audio':'#90a4ae','data_table':'#e57373','struct':'#64b5f6','enum':'#dce775','texture':'#ff8a65','static_mesh':'#80cbc4','skeletal_mesh':'#b39ddb','sequence':'#fff176','game_mode':'#c5e1a5','player_controller':'#80deea','component_bp':'#ef9a9a','external':'#616161','other':'#78909c'};"
			"if(typeof vis==='undefined'){document.getElementById('depgraph-container').innerHTML='<p style=\"padding:40px;color:#ffa07a;text-align:center;\">vis-network library not loaded. The dashboard requires JavaScript libraries that ship with the plugin. Please reinstall from FAB.</p>';}"
			"else if(depData.nodes&&depData.nodes.length>0){"
			"var nodes=depData.nodes.map(function(n){var col=typeColors[n.type]||'#78909c';return{id:n.id,label:n.label,title:n.label+(n.parent_class?' ('+n.parent_class+')':'')+(n.type?' ['+n.type+']':''),color:{background:col,border:'#555',highlight:{background:col,border:'#fff'}},font:{color:'#e0e0e0',size:12},shape:'box',path:n.path||''};});"
			"var edges=depData.edges.map(function(e){return{from:e.from,to:e.to,color:{color:e.type==='inherits'?'#81c784':'#666',highlight:'#fff'},arrows:'to',dashes:e.type==='soft_ref',title:e.type};});"
			"var network=new vis.Network(document.getElementById('depgraph-container'),{nodes:new vis.DataSet(nodes),edges:new vis.DataSet(edges)},{physics:{solver:'forceAtlas2Based',forceAtlas2Based:{gravitationalConstant:-80,springLength:120,springConstant:0.04},stabilization:{iterations:200}},interaction:{hover:true,zoomView:true,dragView:true},nodes:{borderWidth:1,borderWidthSelected:2},edges:{smooth:{type:'continuous'}},layout:{improvedLayout:true}});"
			"network.on('click',function(params){if(params.nodes.length>0){var nid=params.nodes[0];var nd=nodes.find(function(n){return n.id===nid;});if(nd&&nd.path)window.location.href='ue://asset?name='+nd.path;}});"
			"}"
		);

		Html += TEXT("var heatmapData='");
		Html += HeatmapData.Replace(TEXT("'"), TEXT("\\'")).Replace(TEXT("\n"), TEXT("\\n")).Replace(TEXT("\r"), TEXT(""));
		Html += TEXT("';"
			"if(heatmapData.trim()){"
			"var lines=heatmapData.trim().split('\\n');var items=[];"
			"lines.forEach(function(line){var parts=line.trim().split(/\\s+/);if(parts.length>=2){items.push({name:parts[0],score:parseInt(parts[1])||0,type:parts[2]||'other'});}});"
			"if(items.length>0){"
			"items.sort(function(a,b){return b.score-a.score;});"
			"var maxScore=items[0].score;"
			"function getColor(s){var r=maxScore>0?s/maxScore:0;if(r<0.33)return '#4caf50';if(r<0.66)return '#ff9800';return '#f44336';}"
			"var html='<div style=\"display:flex;flex-wrap:wrap;gap:4px;padding:8px;\">';"
			"items.forEach(function(it,i){"
			"var bg=getColor(it.score);"
			"var w=Math.max(80, Math.min(200, 60+it.score*2));"
			"html+='<div onclick=\"window.location.href=\\'ue://asset?name=/Game/'+it.name+'\\'\" style=\"cursor:pointer;background:'+bg+';border-radius:6px;padding:8px 10px;min-width:'+w+'px;border:1px solid rgba(255,255,255,0.1);transition:transform 0.1s;\" onmouseover=\"this.style.transform=\\'scale(1.05)\\'\" onmouseout=\"this.style.transform=\\'scale(1)\\'\">';"
			"html+='<div style=\"font-size:11px;font-weight:600;color:#fff;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;\">'+it.name+'</div>';"
			"html+='<div style=\"font-size:10px;color:rgba(255,255,255,0.7);margin-top:2px;\">'+it.score+' connections &middot; '+it.type+'</div>';"
			"html+='</div>';});"
			"html+='</div>';"
			"html+='<div class=\"legend\"><span><span class=\"dot\" style=\"background:#4caf50\"></span>Low (0-33%%)</span><span><span class=\"dot\" style=\"background:#ff9800\"></span>Medium (33-66%%)</span><span><span class=\"dot\" style=\"background:#f44336\"></span>High (66-100%%)</span></div>';"
			"document.getElementById('heatmap-svg').innerHTML=html;"
			"}}"
		);

		Html += TEXT("var pieData='");
		Html += PieChartData.Replace(TEXT("'"), TEXT("\\'")).Replace(TEXT("\n"), TEXT("\\n")).Replace(TEXT("\r"), TEXT(""));
		Html += TEXT("';"
			"if(pieData.trim()){"
			"var colors=['#4fc3f7','#81c784','#ffb74d','#f06292','#ba68c8','#4db6ac','#ffd54f','#90a4ae','#e57373','#64b5f6'];"
			"var parts=pieData.split(',').map(function(p){var s=p.trim().split(/\\s+/);return{label:s[0],value:parseInt(s[1])||0};});"
			"var total=parts.reduce(function(s,p){return s+p.value;},0);"
			"if(total>0){"
			"var cx=150,cy=150,r=120;"
			"var svg='<svg width=\"450\" height=\"350\" style=\"margin:16px auto;display:block;\">';"
			"var angle=-Math.PI/2;"
			"parts.forEach(function(p,i){var pct=p.value/total;var sweep=pct*2*Math.PI;var x1=cx+r*Math.cos(angle),y1=cy+r*Math.sin(angle);angle+=sweep;var x2=cx+r*Math.cos(angle),y2=cy+r*Math.sin(angle);var large=sweep>Math.PI?1:0;svg+='<path d=\"M'+cx+','+cy+' L'+x1+','+y1+' A'+r+','+r+' 0 '+large+',1 '+x2+','+y2+' Z\" fill=\"'+colors[i%colors.length]+'\"/>';});"
			"svg+='<circle cx=\"'+cx+'\" cy=\"'+cy+'\" r=\"50\" fill=\"#1e1e1e\"/>';"
			"svg+='<text x=\"'+cx+'\" y=\"'+(cy+5)+'\" text-anchor=\"middle\" fill=\"#d4d4d4\" font-size=\"14\">'+total+' total</text></svg>';"
			"var legend='<div class=\"legend\" style=\"flex-wrap:wrap;\">';"
			"parts.forEach(function(p,i){legend+='<span><span class=\"dot\" style=\"background:'+colors[i%colors.length]+'\"></span>'+p.label+' ('+Math.round(p.value/total*100)+'%)</span>';});"
			"legend+='</div>';"
			"document.getElementById('pie-svg').innerHTML=svg+legend;"
			"}}"
		);

		Html += TEXT(
			"if(depData.nodes&&depData.nodes.length>0){"
			"var parentMap={};"
			"depData.nodes.forEach(function(n){if(n.parent_class){if(!parentMap[n.parent_class])parentMap[n.parent_class]=[];parentMap[n.parent_class].push(n);}});"
			"var html='<div style=\"padding:8px;\">';"
			"var parents=Object.keys(parentMap).sort(function(a,b){return parentMap[b].length-parentMap[a].length;});"
			"parents.forEach(function(parent){"
			"var children=parentMap[parent];"
			"var isOpen=children.length<=15;"
			"html+='<div style=\"margin-bottom:12px;border:1px solid #404040;border-radius:8px;overflow:hidden;\">';"
			"html+='<div onclick=\"var c=this.nextElementSibling;c.style.display=c.style.display===\\'none\\'?\\'flex\\':\\'none\\';this.querySelector(\\'span\\').textContent=c.style.display===\\'none\\'?\\'\\u25B6\\':\\'\\u25BC\\'\" style=\"cursor:pointer;padding:10px 14px;background:#2d2d30;display:flex;align-items:center;gap:8px;\">';"
			"html+='<span style=\"color:#888;font-size:12px;\">'+(isOpen?'\\u25BC':'\\u25B6')+'</span>';"
			"html+='<span style=\"color:#4fc3f7;font-weight:600;\">'+parent+'</span>';"
			"html+='<span style=\"color:#888;font-size:12px;\">('+children.length+' blueprints)</span>';"
			"html+='</div>';"
			"html+='<div style=\"display:'+(isOpen?'flex':'none')+';flex-wrap:wrap;gap:6px;padding:10px;background:#252526;\">';"
			"children.sort(function(a,b){return(b.connections||0)-(a.connections||0);});"
			"children.forEach(function(ch){"
			"var col=typeColors[ch.type]||'#78909c';"
			"html+='<div onclick=\"window.location.href=\\'ue://asset?name='+ch.path+'\\'\" style=\"cursor:pointer;background:'+col+';border-radius:4px;padding:6px 10px;font-size:11px;color:#fff;white-space:nowrap;border:1px solid rgba(255,255,255,0.15);\" title=\"'+ch.label+' ['+ch.type+'] '+(ch.connections||0)+' connections\">';"
			"html+=ch.label;"
			"if(ch.connections>0)html+='<span style=\"margin-left:4px;color:rgba(255,255,255,0.6);font-size:10px;\">'+ch.connections+'</span>';"
			"html+='</div>';});"
			"html+='</div></div>';});"
			"html+='</div>';"
			"document.getElementById('inheritance-svg').innerHTML=html;"
			"}"
		);

		if (!PerformanceJson.IsEmpty())
		{
			FString EscapedJson = PerformanceJson;
			EscapedJson.ReplaceInline(TEXT("&"), TEXT("&amp;"));
			EscapedJson.ReplaceInline(TEXT("<"), TEXT("&lt;"));
			EscapedJson.ReplaceInline(TEXT(">"), TEXT("&gt;"));
			EscapedJson.ReplaceInline(TEXT("\""), TEXT("&quot;"));
			EscapedJson.ReplaceInline(TEXT("\r"), TEXT(""));
			EscapedJson.ReplaceInline(TEXT("\n"), TEXT(""));
			Html += TEXT("</script><div id='perf-json-data' style='display:none;'>");
			Html += EscapedJson;
			Html += TEXT("</div><script>");
		}
		Html += TEXT(
			"var perfDataEl=document.getElementById('perf-json-data');"
			"var perfData=null;"
			"if(perfDataEl){try{var raw=perfDataEl.textContent;perfData=JSON.parse(raw);}catch(e){document.getElementById('perf-content').innerHTML='<p style=\"color:#f44\">JSON parse error: '+e.message+'</p>';}}"
			"if(perfData){showTab(4);"
			"try{var pf=perfData;"
			"function escHtml(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;');}"
			"if(pf.success){"
			"var html='';"
			"html+='<div style=\"margin-bottom:16px;display:flex;gap:12px;align-items:center;\">';"
			"html+='<button onclick=\"window.location.href=\\'ue://ai-analyze-perf\\'\" style=\"background:#007acc;color:#fff;border:none;border-radius:6px;padding:10px 20px;font-size:13px;font-weight:600;cursor:pointer;white-space:nowrap;\">Analyze with AI</button>';"
			"html+='<span style=\"color:#888;font-size:12px;\">Send profiling data to the Blueprint Architect for AI-powered optimization recommendations</span>';"
			"html+='</div>';"
			"if(pf.frame_stats&&pf.frame_stats.frame_times_ms){"
			"var ft=pf.frame_stats.frame_times_ms;var W=Math.max(800,window.innerWidth-80);var H=300;"
			"var maxMs=Math.max.apply(null,ft);var yScale=H/(maxMs*1.1);"
			"var xStep=W/ft.length;"
			"html+='<h3 style=\"color:#e0e0e0;margin:0 0 8px;\">Frame Times</h3>';"
			"html+='<div style=\"display:flex;gap:16px;margin-bottom:8px;font-size:12px;color:#888;\">';"
			"html+='<span>Frames: '+pf.frame_stats.game_frame_count+'</span>';"
			"html+='<span>Avg: '+pf.frame_stats.avg_ms+'ms ('+pf.frame_stats.avg_fps+' FPS)</span>';"
			"html+='<span>Min: '+pf.frame_stats.min_ms+'ms</span>';"
			"html+='<span>Max: '+pf.frame_stats.max_ms+'ms</span>';"
			"html+='<span>P99: '+pf.frame_stats.p99_ms+'ms</span>';"
			"html+='</div>';"
			"html+='<svg width=\"'+W+'\" height=\"'+(H+30)+'\" style=\"display:block;margin-bottom:20px;\">';"
			"var y60=H-16.67*yScale;"
			"html+='<line x1=\"0\" y1=\"'+y60+'\" x2=\"'+W+'\" y2=\"'+y60+'\" stroke=\"#4caf50\" stroke-dasharray=\"4\" opacity=\"0.5\"/>';"
			"html+='<text x=\"'+W+'\" y=\"'+(y60-4)+'\" text-anchor=\"end\" fill=\"#4caf50\" font-size=\"10\">60 FPS (16.6ms)</text>';"
			"var y30=H-33.33*yScale;"
			"if(y30>0){html+='<line x1=\"0\" y1=\"'+y30+'\" x2=\"'+W+'\" y2=\"'+y30+'\" stroke=\"#f44336\" stroke-dasharray=\"4\" opacity=\"0.5\"/>';html+='<text x=\"'+W+'\" y=\"'+(y30-4)+'\" text-anchor=\"end\" fill=\"#f44336\" font-size=\"10\">30 FPS (33.3ms)</text>';}"
			"ft.forEach(function(ms,i){"
			"var h=ms*yScale;var y=H-h;"
			"var col=ms<16.67?'#4caf50':ms<33.33?'#ff9800':'#f44336';"
			"html+='<rect x=\"'+(i*xStep)+'\" y=\"'+y+'\" width=\"'+Math.max(1,xStep-0.5)+'\" height=\"'+h+'\" fill=\"'+col+'\" opacity=\"0.8\"/>';"
			"});"
			"html+='</svg>';}"
			"function renderFnTable(title,fns,color){"
			"if(!fns||fns.length===0)return '';"
			"var t='<h3 style=\"color:#e0e0e0;margin:16px 0 8px;\">'+title+'</h3>';"
			"t+='<table style=\"width:100%;border-collapse:collapse;font-size:12px;\">';"
			"t+='<tr style=\"background:#2d2d30;\"><th style=\"padding:6px 10px;text-align:left;border-bottom:1px solid #404040;\">Function</th><th style=\"padding:6px 10px;text-align:right;border-bottom:1px solid #404040;\">Total (ms)</th><th style=\"padding:6px 10px;text-align:right;border-bottom:1px solid #404040;\">Avg (ms)</th><th style=\"padding:6px 10px;text-align:right;border-bottom:1px solid #404040;\">Calls</th></tr>';"
			"fns.forEach(function(fn){"
			"var isBP=fn.name.indexOf('BP_')>=0||fn.name.indexOf('Blueprint')>=0||fn.name.indexOf('Receive')>=0;"
			"var bg=isBP?'rgba(79,195,247,0.1)':'transparent';"
			"var nameCol=isBP?color:'#d4d4d4';"
			"t+='<tr style=\"background:'+bg+';border-bottom:1px solid #333;\"><td style=\"padding:5px 10px;color:'+nameCol+';\">'+escHtml(fn.name)+'</td><td style=\"padding:5px 10px;text-align:right;\">'+fn.total_ms.toFixed(2)+'</td><td style=\"padding:5px 10px;text-align:right;\">'+fn.avg_ms.toFixed(2)+'</td><td style=\"padding:5px 10px;text-align:right;\">'+fn.count+'</td></tr>';"
			"});t+='</table>';return t;}"
			"html+=renderFnTable('Top CPU Functions',pf.top_cpu_functions,'#4fc3f7');"
			"html+=renderFnTable('Top GPU Functions',pf.top_gpu_functions,'#ba68c8');"
			"if(pf.counters&&Object.keys(pf.counters).length>0){"
			"html+='<h3 style=\"color:#e0e0e0;margin:16px 0 8px;\">Counters</h3>';"
			"html+='<div style=\"display:flex;flex-wrap:wrap;gap:8px;\">';"
			"Object.keys(pf.counters).sort().forEach(function(k){"
			"var v=pf.counters[k];"
			"var display=typeof v==='number'?(v>1000000?(v/1000000).toFixed(1)+'M':v>1000?(v/1000).toFixed(1)+'K':v.toFixed(0)):v;"
			"html+='<div style=\"background:#2d2d30;border-radius:4px;padding:6px 10px;border:1px solid #404040;font-size:11px;\"><div style=\"color:#888;\">'+escHtml(k)+'</div><div style=\"color:#d4d4d4;font-size:14px;font-weight:600;\">'+display+'</div></div>';"
			"});html+='</div>';}"
			"if(pf.memory&&pf.memory.tags&&pf.memory.tags.length>0){"
			"html+='<h3 style=\"color:#e0e0e0;margin:16px 0 8px;\">Memory Breakdown ('+pf.memory.total_mb.toFixed(1)+' MB total)</h3>';"
			"var maxMb=pf.memory.tags[0].mb;"
			"pf.memory.tags.forEach(function(t){"
			"var pct=maxMb>0?(t.mb/maxMb*100):0;"
			"html+='<div style=\"display:flex;align-items:center;gap:8px;margin:3px 0;font-size:12px;\">';"
			"html+='<span style=\"min-width:120px;color:#d4d4d4;\">'+t.name+'</span>';"
			"html+='<div style=\"flex:1;height:16px;background:#2d2d30;border-radius:3px;overflow:hidden;\"><div style=\"width:'+pct+'%;height:100%;background:#4fc3f7;border-radius:3px;\"></div></div>';"
			"html+='<span style=\"min-width:60px;text-align:right;color:#888;\">'+t.mb.toFixed(1)+' MB</span>';"
			"html+='</div>';});"
			"}"
			"html+='<div style=\"margin-top:12px;padding:10px;background:#2d2d30;border-radius:6px;font-size:11px;color:#888;\">';"
			"var dur=pf.gameplay_seconds?pf.gameplay_seconds.toFixed(1)+'s gameplay':''+pf.duration_seconds.toFixed(1)+'s trace';"
			"html+='Trace: '+escHtml(pf.trace_path)+'<br>Size: '+pf.trace_size_mb.toFixed(1)+' MB &middot; '+dur;"
			"if(pf.requested_duration)html+=' (requested: '+pf.requested_duration+'s)';"
			"html+='</div>';"
			"document.getElementById('perf-content').innerHTML=html;"
			"}else{document.getElementById('perf-content').innerHTML='<p style=\"color:#ff9800\">Profile data present but success=false. Keys: '+Object.keys(pf).join(', ')+'</p>';}"
			"}catch(e){document.getElementById('perf-content').innerHTML='<p style=\"color:#f44\">Error: '+e.message+'</p><pre style=\"color:#888;font-size:11px;max-height:200px;overflow:auto;\">'+String(perfData).substring(0,500)+'</pre>';}"
			"}"
		);

		Html += TEXT(
			"(function(){"
			"var hint=document.createElement('div');"
			"hint.style.cssText='position:absolute;top:12px;right:12px;background:rgba(0,0,0,.55);color:#aaa;font-size:11px;padding:5px 10px;border-radius:6px;pointer-events:none;z-index:99;';"
			"hint.textContent='Drag to pan \u2022 Ctrl+Scroll to zoom';"
			"var p3=document.getElementById('panel-3');if(p3){p3.style.position='relative';p3.appendChild(hint.cloneNode(true));}"
			"var p1=document.getElementById('panel-1');if(p1){p1.style.position='relative';p1.appendChild(hint.cloneNode(true));}"
			"})();"
		);

		Html += TEXT(
			"function setupZoomPan(panelId){"
			"var panel=document.getElementById(panelId);if(!panel)return;"
			"var zoom=100,panX=0,panY=0,isDragging=false,startX,startY;"
			"var inner=panel.querySelector('div');"
			"if(!inner)return;"
			"function update(){inner.style.transform='translate('+panX+'px,'+panY+'px) scale('+(zoom/100)+')';inner.style.transformOrigin='0 0';}"
			"panel.addEventListener('wheel',function(e){if(e.ctrlKey){e.preventDefault();zoom+=e.deltaY>0?-10:10;zoom=Math.max(20,Math.min(500,zoom));update();}},{passive:false});"
			"panel.addEventListener('mousedown',function(e){if(e.button===0){isDragging=true;startX=e.clientX-panX;startY=e.clientY-panY;panel.style.cursor='grabbing';e.preventDefault();}});"
			"panel.addEventListener('mousemove',function(e){if(isDragging){panX=e.clientX-startX;panY=e.clientY-startY;update();}});"
			"panel.addEventListener('mouseup',function(){isDragging=false;panel.style.cursor='grab';});"
			"panel.style.cursor='grab';panel.style.overflow='hidden';"
			"}"
			"setupZoomPan('panel-1');"
			"setupZoomPan('panel-3');"
		);

		Html += TEXT("</script></body></html>");

		TSharedRef<SWindow> DashWindow = SNew(SWindow)
			.Title(FText::FromString(TEXT("Project Dashboard")))
			.ClientSize(FVector2D(1280, 800))
			.SupportsMaximize(true)
			.SupportsMinimize(true)
			.AutoCenter(EAutoCenter::PreferredWorkArea);

		TSharedPtr<SWebBrowser> DashBrowser;
		TSharedPtr<FString> StoredHtml = MakeShared<FString>(Html);
		DashWindow->SetContent(
			SAssignNew(DashBrowser, SWebBrowser)
			.InitialURL(TEXT("about:blank"))
			.ShowErrorMessage(false)
			.SupportsTransparency(false)
			.ShowControls(false)
			.ShowAddressBar(false)
			.OnBeforeNavigation_Lambda([OnLoadUrl](const FString& Url, const FWebNavigationRequest& ) -> bool {
				if (Url.StartsWith(TEXT("ue://asset?name=")) ||
					Url.StartsWith(TEXT("ue://asset?path=")) ||
					Url.StartsWith(TEXT("ue://analyze-trace?path=")) ||
					Url.StartsWith(TEXT("ue://ai-analyze-perf")))
				{
					if (OnLoadUrl.IsBound())
					{
						FString Discard;
						OnLoadUrl.Execute(TEXT("GET"), Url, Discard);
					}
					return true;
				}
				if (Url.StartsWith(TEXT("ue://upgrade")))
				{
					/* Axivor AI: no external upgrade page. */
					return true;
				}
				return false;
			})
			.OnLoadUrl_Lambda([OnLoadUrl, StoredHtml](const FString& Method, const FString& Url, FString& Response) -> bool {
				if (Url.StartsWith(TEXT("ue://asset?name=")) || Url.StartsWith(TEXT("ue://analyze-trace?path=")))
				{
					if (OnLoadUrl.IsBound())
					{
						FString Discard;
						OnLoadUrl.Execute(Method, Url, Discard);
					}
					Response = *StoredHtml;
					return true;
				}
				if (Url.StartsWith(TEXT("ue://ai-analyze-perf")))
				{
					if (OnLoadUrl.IsBound())
					{
						FString Discard;
						OnLoadUrl.Execute(Method, Url, Discard);
					}
					Response = TEXT("<!DOCTYPE html><html><body style='background:#1e1e1e;color:#888;padding:40px;text-align:center;font-family:Segoe UI,Arial;'><p>Sending to AI Architect...</p></body></html>");
					return true;
				}
				return false;
			})
		);

		if (DashBrowser.IsValid() && GEditor)
		{
			TArray<uint8> HtmlBytes;
			FTCHARToUTF8 Conv(*Html);
			HtmlBytes.Append((const uint8*)Conv.Get(), Conv.Length());
			FString DataUri = TEXT("data:text/html;base64,") + FBase64::Encode(HtmlBytes);

			TWeakPtr<SWebBrowser> WeakBrowser = DashBrowser;
			TSharedPtr<FString> UriCopy = MakeShared<FString>(DataUri);
			FTimerHandle DashLoadTimer;
			GEditor->GetTimerManager()->SetTimer(DashLoadTimer, [WeakBrowser, UriCopy]()
			{
				if (auto B = WeakBrowser.Pin())
					B->LoadURL(*UriCopy);
			}, 0.3f, false);
		}

		return DashWindow;
	}
}
