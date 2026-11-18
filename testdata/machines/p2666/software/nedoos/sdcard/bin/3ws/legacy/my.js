var curDir='';
var s;
var xhr;
var fileToUp;
var fCnt;
var r;
var API_BASE = (function() {
	var path = location.pathname || '/';
	if (/\/legacy\/[^/]*$/i.test(path)) {
		return path.replace(/\/legacy\/[^/]*$/i, '/');
	}
	if (/\/index\.htm?$/i.test(path)) {
		return path.replace(/\/index\.htm?$/i, '/');
	}
	if (path.slice(-1) === '/') {
		return path;
	}
	return path.replace(/\/[^/]*$/, '/') || '/';
})();
var cyr866="АБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдежзийклмноп░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀рстуфхцчшщъыьэюяЁёЄєЇїЎў°∙·√№¤■ ";
function to866(str){
	var s866="";
	for(var i=0;i<str.length;i++){
		var ch=str.charCodeAt(i);
		if(ch>127){
			ch=cyr866.indexOf(str[i])+128;
			if(ch>=0){s866+="%"+ch.toString(16).toUpperCase();}
			else{s866+="x";}
		}else{
			s866+=str[i];
		}
	}
	return s866;
}
function myGet(up, us) {
	var r = new XMLHttpRequest();
	us = to866(us)
	if (up=='?d='){
		us = encodeURIComponent(us)
	}
	r.open("GET", API_BASE + up + us + "&r=" + Math.random(), false);
	try {
		r.setRequestHeader('Connection', 'close');
	} catch (e) {
	}
	r.send(null);	
	return r.responseText;
}

function mkdir(){
	var ss = window.curDir+'/'+document.getElementById('dirName').value;
	document.getElementById('log').innerHTML=myGet('?m=', ss);
	rddir(window.curDir);
}

function unlink(dirPath){ 
	var ss = window.curDir+'/'+dirPath;
	document.getElementById('log').innerHTML=myGet('?u=', ss);
	rddir(window.curDir);
}
function runprog(dirPath){ 
	var ss = dirPath;
	document.getElementById('log').innerHTML=myGet('?s=', ss);
}
function compareFileInfo(finfoA, finfoB) {
	if(finfoA.isdir==3 || finfoB.isdir==3) return 0;
	return finfoA.fn.localeCompare(finfoB.fn);
}
function rddir(dirPath){
	var i=0;
	var k=0;
	window.s='';
	window.curDir=dirPath;
	window.s+='Current dir: <a href="javascript:rddir(\'\')">0:</a>';
	if(dirPath!=''){
		while((k=dirPath.indexOf('/',i))!=-1){
			window.s+='<a href="javascript:rddir(\''+dirPath.substring(0,k)+'\')">'+
				dirPath.substring(i,k)+'</a>/';
			i=k+1;
		}
		window.s+=dirPath.substring(i);
	}
	window.s+='<br><table  id="divdir">';
	try {
		j = JSON.parse(myGet('?d=', dirPath));
	} catch (e) {
		window.s += '</table>';
		document.getElementById('divlog').innerHTML = window.s + '<br><span style="color:red">read dir error</span>';
		return;
	}
	j.fno.sort(compareFileInfo);
	j.fno.forEach(function(item, i, arr) {
		var n,pn;
		if(item.isdir==1){
			n=item.fn;
			var dt_opt = { year: 'numeric', month: 'numeric', day: 'numeric',
			  hour: 'numeric', minute: 'numeric'};
			dt = new Date((item.dt>>9)+1980, (item.dt>>5)%16-1, item.dt%32, (item.tm>>11)%32, (item.tm>>5)%64, (item.tm%32)*2, 0);
			if(n=='..')pn=dirPath.substr(0,dirPath.lastIndexOf('/'));
			else pn=((dirPath=='')?(''):(dirPath+'/'))+n;
			if(n!="."){
				window.s+='<tr>';
				
				window.s+='<td><a href="javascript:rddir(\''+pn+'\')">'+n+'</a></td><td></td><td>'+ dt.toLocaleString("ru", dt_opt) +'</td><td></td><td>';
				
				window.s+='</td><td><a href="javascript:unlink(\''+n+'\')">Remove</a><td>';
				window.s+='</tr>';
			}
		}
	});	
	j.fno.forEach(function(item, i, arr) {
		var n,pn,iof;
		if(item.isdir==0){
			
			var dt_opt = { year: 'numeric', month: 'numeric', day: 'numeric',
			  hour: 'numeric', minute: 'numeric'};
			dt = new Date((item.dt>>9)+1980, (item.dt>>5)%16-1, item.dt%32, (item.tm>>11)%32, (item.tm>>5)%64, (item.tm%32)*2, 0);

			n=item.fn;
			pn=((dirPath=='/')?(''):(dirPath+'/'))+n;
			window.s+='<tr>';
			window.s+='<td>'+n+'</td><td>'+item.sz+'B </td><td>'+ dt.toLocaleString("ru", dt_opt) 
				+'</td><td>'+'<a href="'+API_BASE+'?g='+encodeURIComponent(pn)+'">Download</a></td>';
			iof=n.lastIndexOf('.');
			if(iof != -1){
				switch(n.toLowerCase().substring(iof)){
					case '.com':
						window.s+='<td><a href="javascript:runprog(\''+pn+'\')">Run</a></td>';
						break;					
					case '.pt3':	
					case '.pt2':
					case '.tfc':
					case '.m':
					case '.mt3':
					case '.et':
					case '.etc':
					case '.cmp':
					case '.tfd':
					case '.tfm':				
						window.s+='<td><a href="javascript:runprog(\'bin/player.com%20/'+pn+'\')">Play</a></td>';
						break;
					case '.mod':				
						window.s+='<td><a href="javascript:runprog(\'bin/modplay.com%20/'+pn+'\')">Play</a></td>';
						break;
					case '.mp3':
					case '.mid':
					case '.ogg':
					case '.aac':
					case '.mdr':
					case '.mwm':
						window.s+='<td><a href="javascript:runprog(\'bin/gp.com%20/'+pn+'\')">Play</a></td>';
						break;
					case '.16c':
					case '.fnt':
					case '.img':
					case '.3':
					case '.888':
					case '.y':
					case '.+':
					case '.-':
					case '.plc':
					case '.mc ':
					case '.mcx':
					case '.grf':
					case '.ch$':
					case '.mg1':
					case '.mg2':
					case '.mg4':
					case '.mg8':
					case '.rm':
					case '.mlt':
					case '.53c':						
					case '.zxs':
					case '.atr':
					case '.scr':
						window.s+='<td><a href="javascript:runprog(\'bin/view.com%20/'+pn+'\')">View</a></td>';
						break;
					case '.gif':
					case '.jpg':
					case '.png':
					case '.htm':
					case '.svg':
						window.s+='<td><a href="javascript:runprog(\'bin/browser.com%20/'+pn+'\')">View</a></td>';
						break;
					case '.bmp':
						window.s+='<td><a href="javascript:runprog(\'bin/scratch.com%20/'+pn+'\')">View</a></td>';
						break;	
					default:
						window.s+='<td></td>';
						break;
				}
			}else 
				window.s+='<td></td>';
			/*
			if(n.toLowerCase().substring(n.length-4)=='.com')
				window.s+='<td><a href="javascript:runprog(\''+pn+'\')">Run</a></td>';	
			else if(n.toLowerCase().substring(n.length-4)=='.pt3'){
				window.s+='<td><a href="javascript:runprog(\'bin/player.com%20/'+pn+'\')">Play</a></td>';	
			}
			else 
				window.s+='<td></td>';
			*/
			window.s+='</td><td><a href="javascript:unlink(\''+n+'\')">Remove</a></td>';
			window.s+='</tr>';
		}
	});

	s+='</table>';
	document.getElementById('divlog').innerHTML=s;
}

function log(html) {
	document.getElementById('log').innerHTML = html;
}
function endUp(){
	if(window.r!==undefined){
		if(window.r.response=="") {
			setTimeout(endUp, 100);
			return;
		}
	}
	window.fcnt--;
	if(window.fcnt>0){
		inpFilePtr();
	}else{
		rddir(window.curDir);	
		window.fileToUp.files=null; 
		document.getElementById('btnUpload').innerHTML='upload';
		window.r = new XMLHttpRequest();
	}
}
fileToUp = document.getElementById("fileToUp");
function inpFilePtr() {
	var cf=window.fileToUp.files[window.fileToUp.files.length-window.fcnt];
	document.getElementById('btnUpload').innerHTML=cf.name+'...';
	window.r = new XMLHttpRequest();
	window.r.open("PUT", API_BASE + to866(((window.curDir=='')?'':(window.curDir+'/'))+cf.name), true);
	window.r.upload.onprogress = function(event) {
		document.getElementById('btnUpload').innerHTML=cf.name+' '+event.loaded + '/' + event.total;}
	window.r.upload.onload = endUp;
	window.r.upload.onerror = function(event) { 
		inpFilePtr();}
	window.r.send(cf);
}
fcnt=0;
if (fileToUp) {
	fileToUp.oninput = function(){
		window.fcnt=window.fileToUp.files.length;
	    if(window.fcnt<=0) return;
		inpFilePtr();
	};
}
var UI_VERSION_KEY = '3ws-ui-version';
var UI_LANG_KEY = '3ws-lang';

function isRussianUiLang() {
	try {
		var stored = localStorage.getItem(UI_LANG_KEY);
		if (stored === 'en') {
			return false;
		}
		if (stored === 'ru') {
			return true;
		}
	} catch (e) {
	}
	if (navigator.languages && navigator.languages.length) {
		for (var i = 0; i < navigator.languages.length; i++) {
			var code = String(navigator.languages[i] || '').toLowerCase().replace(/_/g, '-');
			if (code === 'ru' || code.indexOf('ru-') === 0) {
				return true;
			}
		}
	} else if (navigator.language) {
		var lang = String(navigator.language).toLowerCase().replace(/_/g, '-');
		if (lang === 'ru' || lang.indexOf('ru-') === 0) {
			return true;
		}
	}
	return false;
}

function switchToModernVersion() {
	try {
		localStorage.setItem(UI_VERSION_KEY, 'modern');
	} catch (e) {
	}
	window.location.replace(API_BASE + 'index.htm');
}

function updateVersionSwitchLink() {
	var el = document.getElementById('linkNewVersion');
	if (!el) {
		return;
	}
	el.textContent = isRussianUiLang()
		? '\u041d\u043e\u0432\u0430\u044f \u0432\u0435\u0440\u0441\u0438\u044f'
		: 'New version';
}

updateVersionSwitchLink();
log('status');
rddir('');

