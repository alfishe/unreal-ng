var curDir = '';
var uploadCurDir = '';
var uploadQueue = [];
var uploadActive = false;
var uploadTotalCount = 0;
var uploadDoneCount = 0;
var uploadBatchTotalBytes = 0;
var uploadBatchDoneBytes = 0;
var deleteTotalCount = 0;
var deleteDoneCount = 0;
var selectedItems = {};
var lastListDir = '';
var DOWNLOAD_GAP_MS = 100;
var DOWNLOAD_MAX_RETRIES = 3;
var DOWNLOAD_RETRY_BASE_MS = 600;
var DOWNLOAD_RETRY_STEP_MS = 350;
var REQUEST_TIMEOUT_MS = 60000;
var downloadCancelled = false;
var downloadJobActive = false;
var activeDownloadXhr = null;
var uploadCancelled = false;
var uploadJobActive = false;
var activeUploadXhr = null;
var deleteJobActive = false;
var readDirBusy = false;
var MOBILE_UI_MQ = '(max-width: 768px)';
var mobileUiMq = null;
var mobileUiActive = null;
var rowActions = [];

var UI_LANG_KEY = '3ws-lang';
var UI_VERSION_KEY = '3ws-ui-version';
var uiLang = 'ru';
var T = {};

var I18N = {
	ru: {
		appTitle: 'ZX File Manager',
		refresh: '\u041e\u0431\u043d\u043e\u0432\u0438\u0442\u044c',
		dirPlaceholder: '\u0418\u043c\u044f \u043d\u043e\u0432\u043e\u0439 \u043f\u0430\u043f\u043a\u0438',
		mkdir: '\u0421\u043e\u0437\u0434\u0430\u0442\u044c \u043f\u0430\u043f\u043a\u0443',
		uploadFiles: '\u0417\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044c \u0444\u0430\u0439\u043b\u044b',
		uploadFolder: '\u0417\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044c \u043f\u0430\u043f\u043a\u0443',
		dropHint: '\u041f\u0435\u0440\u0435\u0442\u0430\u0449\u0438\u0442\u0435 \u0444\u0430\u0439\u043b\u044b \u0438\u043b\u0438 \u043f\u0430\u043f\u043a\u0438 \u0441\u044e\u0434\u0430 \u0434\u043b\u044f \u0437\u0430\u0433\u0440\u0443\u0437\u043a\u0438',
		enterDirName: '\u0412\u0432\u0435\u0434\u0438\u0442\u0435 \u0438\u043c\u044f \u043f\u0430\u043f\u043a\u0438',
		deleteFolder: '\u043f\u0430\u043f\u043a\u0443',
		deleteFile: '\u0444\u0430\u0439\u043b',
		deleteConfirm: '\u0423\u0434\u0430\u043b\u0438\u0442\u044c',
		deleteConfirmFolder: '\u0423\u0434\u0430\u043b\u0438\u0442\u044c \u043f\u0430\u043f\u043a\u0443 \u0438 \u0432\u0441\u0451 \u0441\u043e\u0434\u0435\u0440\u0436\u0438\u043c\u043e\u0435',
		deletingFolder: '\u0423\u0434\u0430\u043b\u0435\u043d\u0438\u0435 \u043f\u0430\u043f\u043a\u0438',
		deleteProgress: '\u0423\u0434\u0430\u043b\u0435\u043d\u0438\u0435',
		deleteFolderDone: '\u041f\u0430\u043f\u043a\u0430 \u0443\u0434\u0430\u043b\u0435\u043d\u0430',
		deleteError: '\u041e\u0448\u0438\u0431\u043a\u0430 \u0443\u0434\u0430\u043b\u0435\u043d\u0438\u044f',
		colName: '\u0418\u043c\u044f',
		colSize: '\u0420\u0430\u0437\u043c\u0435\u0440',
		colDate: '\u0418\u0437\u043c\u0435\u043d\u0451\u043d',
		colActions: '\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u044f',
		downloadZip: '\u0421\u043a\u0430\u0447\u0430\u0442\u044c ZIP',
		download: '\u0421\u043a\u0430\u0447\u0430\u0442\u044c',
		remove: '\u0423\u0434\u0430\u043b\u0438\u0442\u044c',
		view: '\u041f\u0440\u043e\u0441\u043c\u043e\u0442\u0440',
		run: '\u0417\u0430\u043f\u0443\u0441\u043a',
		play: '\u041f\u0440\u043e\u0438\u0433\u0440\u0430\u0442\u044c',
		emptyDir: '\u041f\u0430\u043f\u043a\u0430 \u043f\u0443\u0441\u0442\u0430',
		readDirError: '\u041d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u043f\u0440\u043e\u0447\u0438\u0442\u0430\u0442\u044c \u043a\u0430\u0442\u0430\u043b\u043e\u0433',
		dirError: '\u041e\u0448\u0438\u0431\u043a\u0430 \u0447\u0442\u0435\u043d\u0438\u044f \u043a\u0430\u0442\u0430\u043b\u043e\u0433\u0430',
		readingDir: '\u0427\u0442\u0435\u043d\u0438\u0435 \u043a\u0430\u0442\u0430\u043b\u043e\u0433\u0430',
		mkdirProgress: '\u0421\u043e\u0437\u0434\u0430\u043d\u0438\u0435 \u043f\u0430\u043f\u043a\u0438',
		deletingFile: '\u0423\u0434\u0430\u043b\u0435\u043d\u0438\u0435 \u0444\u0430\u0439\u043b\u0430',
		running: '\u0417\u0430\u043f\u0443\u0441\u043a',
		muteGs: 'Mute GS',
		muteAy: 'Mute AY',
		mutingGs: 'Mute GS...',
		mutingAy: 'Mute AY...',
		stopDaemon: '\u041e\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u044c \u0441\u0435\u0440\u0432\u0435\u0440',
		stoppingDaemon: '\u041e\u0441\u0442\u0430\u043d\u043e\u0432\u043a\u0430 \u0441\u0435\u0440\u0432\u0435\u0440\u0430',
		noUploadFiles: '\u041d\u0435\u0442 \u0444\u0430\u0439\u043b\u043e\u0432 \u0434\u043b\u044f \u0437\u0430\u0433\u0440\u0443\u0437\u043a\u0438',
		uploadDone: '\u0417\u0430\u0433\u0440\u0443\u0437\u043a\u0430 \u0437\u0430\u0432\u0435\u0440\u0448\u0435\u043d\u0430',
		uploadError: '\u041e\u0448\u0438\u0431\u043a\u0430 \u0437\u0430\u0433\u0440\u0443\u0437\u043a\u0438',
		uploadProgress: '\u0417\u0430\u0433\u0440\u0443\u0437\u043a\u0430',
		zipPrepare: '\u041f\u043e\u0434\u0433\u043e\u0442\u043e\u0432\u043a\u0430 \u0430\u0440\u0445\u0438\u0432\u0430',
		collectFiles: '\u0421\u0431\u043e\u0440 \u0444\u0430\u0439\u043b\u043e\u0432',
		collectedFiles: '\u0421\u043e\u0431\u0440\u0430\u043d\u043e \u0444\u0430\u0439\u043b\u043e\u0432',
		emptyDownload: '\u041f\u0430\u043f\u043a\u0430 \u043f\u0443\u0441\u0442\u0430, \u043d\u0435\u0447\u0435\u0433\u043e \u0441\u043a\u0430\u0447\u0438\u0432\u0430\u0442\u044c',
		creatingZip: '\u0421\u043e\u0437\u0434\u0430\u043d\u0438\u0435 ZIP',
		zipReady: '\u0410\u0440\u0445\u0438\u0432 \u0433\u043e\u0442\u043e\u0432',
		filesWord: '\u0444\u0430\u0439\u043b\u043e\u0432',
		downloadError: '\u041e\u0448\u0438\u0431\u043a\u0430 \u0441\u043a\u0430\u0447\u0438\u0432\u0430\u043d\u0438\u044f \u043f\u0430\u043f\u043a\u0438',
		downloadSizeError: '\u0420\u0430\u0437\u043c\u0435\u0440 \u0444\u0430\u0439\u043b\u0430 \u043d\u0435 \u0441\u043e\u0432\u043f\u0430\u0434\u0430\u0435\u0442',
		cancel: '\u041e\u0442\u043c\u0435\u043d\u0430',
		downloadCancelled: '\u0421\u043a\u0430\u0447\u0438\u0432\u0430\u043d\u0438\u0435 \u043e\u0442\u043c\u0435\u043d\u0435\u043d\u043e',
		uploadCancelled: '\u0417\u0430\u0433\u0440\u0443\u0437\u043a\u0430 \u043e\u0442\u043c\u0435\u043d\u0435\u043d\u0430',
		selectedLabel: '\u044d\u043b\u0435\u043c\u0435\u043d\u0442\u043e\u0432 \u0432\u044b\u0431\u0440\u0430\u043d\u043e',
		downloadSelected: '\u0421\u043a\u0430\u0447\u0430\u0442\u044c \u0432\u044b\u0431\u0440\u0430\u043d\u043d\u043e\u0435',
		deleteSelected: '\u0423\u0434\u0430\u043b\u0438\u0442\u044c \u0432\u044b\u0431\u0440\u0430\u043d\u043d\u043e\u0435',
		clearSelection: '\u0421\u043d\u044f\u0442\u044c \u0432\u044b\u0431\u043e\u0440',
		nothingSelected: '\u041d\u0438\u0447\u0435\u0433\u043e \u043d\u0435 \u0432\u044b\u0431\u0440\u0430\u043d\u043e',
		deleteSelectedConfirm: '\u0423\u0434\u0430\u043b\u0438\u0442\u044c \u0432\u044b\u0431\u0440\u0430\u043d\u043d\u044b\u0435 \u044d\u043b\u0435\u043c\u0435\u043d\u0442\u044b',
		deleteSelectedDone: '\u0412\u044b\u0431\u0440\u0430\u043d\u043d\u043e\u0435 \u0443\u0434\u0430\u043b\u0435\u043d\u043e',
		selectedZipName: 'selected.zip',
		langRu: '\u0420\u0443\u0441\u0441\u043a\u0438\u0439',
		langEn: 'English',
		oldVersion: '\u0421\u0442\u0430\u0440\u0430\u044f \u0432\u0435\u0440\u0441\u0438\u044f',
		dash: '-'
	},
	en: {
		appTitle: 'ZX File Manager',
		refresh: 'Refresh',
		dirPlaceholder: 'New folder name',
		mkdir: 'Create folder',
		uploadFiles: 'Upload files',
		uploadFolder: 'Upload folder',
		dropHint: 'Drop files or folders here to upload',
		enterDirName: 'Enter folder name',
		deleteFolder: 'folder',
		deleteFile: 'file',
		deleteConfirm: 'Delete',
		deleteConfirmFolder: 'Delete folder and all contents',
		deletingFolder: 'Deleting folder',
		deleteProgress: 'Deleting',
		deleteFolderDone: 'Folder deleted',
		deleteError: 'Delete error',
		colName: 'Name',
		colSize: 'Size',
		colDate: 'Modified',
		colActions: 'Actions',
		downloadZip: 'Download ZIP',
		download: 'Download',
		remove: 'Delete',
		view: 'View',
		run: 'Run',
		play: 'Play',
		emptyDir: 'Folder is empty',
		readDirError: 'Could not read directory',
		dirError: 'Directory read error',
		readingDir: 'Reading directory',
		mkdirProgress: 'Creating folder',
		deletingFile: 'Deleting file',
		running: 'Running',
		muteGs: 'Mute GS',
		muteAy: 'Mute AY',
		mutingGs: 'Mute GS...',
		mutingAy: 'Mute AY...',
		stopDaemon: 'Stop daemon',
		stoppingDaemon: 'Stopping server',
		noUploadFiles: 'No files to upload',
		uploadDone: 'Upload complete',
		uploadError: 'Upload error',
		uploadProgress: 'Upload',
		zipPrepare: 'Preparing archive',
		collectFiles: 'Collecting files',
		collectedFiles: 'Files collected',
		emptyDownload: 'Folder is empty, nothing to download',
		creatingZip: 'Creating ZIP',
		zipReady: 'Archive ready',
		filesWord: 'files',
		downloadError: 'Download error',
		downloadSizeError: 'File size mismatch',
		cancel: 'Cancel',
		downloadCancelled: 'Download cancelled',
		uploadCancelled: 'Upload cancelled',
		selectedLabel: 'items selected',
		downloadSelected: 'Download selected',
		deleteSelected: 'Delete selected',
		clearSelection: 'Clear selection',
		nothingSelected: 'Nothing selected',
		deleteSelectedConfirm: 'Delete selected items',
		deleteSelectedDone: 'Selection deleted',
		selectedZipName: 'selected.zip',
		langRu: 'Russian',
		langEn: 'English',
		oldVersion: 'Old version',
		dash: '-'
	}
};

function getApiBase() {
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
}

function apiUrl(part) {
	return getApiBase() + (part || '');
}

function switchToLegacyVersion() {
	try {
		localStorage.setItem(UI_VERSION_KEY, 'legacy');
	} catch (e) {
	}
	window.location.replace(versionSwitchUrl('legacy/index.htm'));
}

function versionSwitchUrl(path) {
	return apiUrl(path);
}

function isRussianBrowserLang() {
	var langs = [];
	if (navigator.languages && navigator.languages.length) {
		langs = navigator.languages;
	} else if (navigator.language) {
		langs = [navigator.language];
	} else if (navigator.userLanguage) {
		langs = [navigator.userLanguage];
	} else if (navigator.browserLanguage) {
		langs = [navigator.browserLanguage];
	}
	for (var i = 0; i < langs.length; i++) {
		var code = String(langs[i] || '').toLowerCase().replace(/_/g, '-');
		if (code === 'ru' || code.indexOf('ru-') === 0) {
			return true;
		}
	}
	return false;
}

function detectBrowserLang() {
	return isRussianBrowserLang() ? 'ru' : 'en';
}

function getSavedLang() {
	try {
		var stored = localStorage.getItem(UI_LANG_KEY);
		if (stored === 'ru' || stored === 'en') {
			return stored;
		}
	} catch (e) {
	}
	return null;
}

function resolveUiLang() {
	var saved = getSavedLang();
	if (saved) {
		return saved;
	}
	return detectBrowserLang();
}

function updateLangSelectOptions() {
	var sel = $('langSelect');
	if (!sel) {
		return;
	}
	sel.options[0].text = T.langRu;
	sel.options[1].text = T.langEn;
	sel.value = uiLang;
}

function applyStaticUi() {
	document.title = T.appTitle;
	if ($('appTitle')) {
		$('appTitle').textContent = T.appTitle;
	}
	if ($('btnStopDaemon')) {
		$('btnStopDaemon').textContent = T.stopDaemon;
	}
	if ($('linkOldVersion')) {
		$('linkOldVersion').textContent = T.oldVersion;
	}
	initUI();
	updateLangSelectOptions();
	updateSelectionBar();
}

function setLang(lang, persist) {
	if (!I18N[lang]) {
		lang = 'ru';
	}
	uiLang = lang;
	T = I18N[lang];
	if (persist !== false) {
		try {
			localStorage.setItem(UI_LANG_KEY, lang);
		} catch (e) {
		}
	}
	document.documentElement.lang = lang;
	applyStaticUi();
}

function bindLangSelect() {
	var sel = $('langSelect');
	if (!sel) {
		return;
	}
	sel.onchange = function() {
		setLang(sel.value, true);
		rddir(curDir);
	};
}

function isMobileUi() {
	if (!mobileUiMq && window.matchMedia) {
		mobileUiMq = window.matchMedia(MOBILE_UI_MQ);
	}
	return mobileUiMq ? mobileUiMq.matches : false;
}

function updateMobileUiLayout() {
	var mobile = isMobileUi();
	var app = $('app');
	if (app) {
		app.classList.toggle('mobile-ui', mobile);
	}
	document.body.classList.toggle('mobile-ui', mobile);
	if (mobileUiActive !== null && mobile !== mobileUiActive) {
		if (mobile) {
			clearSelection();
		}
		rddir(curDir);
	}
	mobileUiActive = mobile;
}

function bindMobileUi() {
	if (!window.matchMedia) {
		updateMobileUiLayout();
		return;
	}
	if (!mobileUiMq) {
		mobileUiMq = window.matchMedia(MOBILE_UI_MQ);
	}
	updateMobileUiLayout();
	if (mobileUiMq.addEventListener) {
		mobileUiMq.addEventListener('change', updateMobileUiLayout);
	} else if (mobileUiMq.addListener) {
		mobileUiMq.addListener(updateMobileUiLayout);
	}
}

function renderMobileCardClass(name, isDir) {
	var cls = 'file-card';
	if (isItemSelected(name, isDir)) {
		cls += ' selected';
	}
	return cls;
}

function renderMobileCardActions(innerHtml) {
	if (!innerHtml) {
		return '';
	}
	return '<div class="file-card-actions">' + innerHtml + '</div>';
}

function renderMobileFolderNav(name, iconKind, nextPath, metaHtml) {
	var id = registerRowAction(function() {
		rddir(nextPath);
	});
	var html = '<a class="file-card-nav" href="javascript:void(0)" onclick="runRowAction(' + id + ')">';
	html += '<span class="file-name">' + fileIcon(iconKind) + escapeHtml(name) + '</span>';
	if (metaHtml) {
		html += metaHtml;
	}
	html += '</a>';
	return html;
}

function renderMobileDirectory(items, dirPath) {
	var html = '<div class="file-cards">';
	var hasRows = false;

	items.forEach(function(item) {
		if (item.isdir !== 1 || item.fn === '.') {
			return;
		}
		hasRows = true;
		var name = item.fn;
		var nextPath = name === '..' ? getParentPath(dirPath) : joinPath(dirPath, name);
		var cardClass = renderMobileCardClass(name, true);
		if (name === '..') {
			cardClass += ' file-card-up';
		} else {
			cardClass += ' file-card-folder';
		}
		var actionsHtml = '';
		if (name !== '..') {
			actionsHtml = actionButton(T.downloadZip, 'btn-secondary action-dl', function() {
				downloadFolder(name);
			});
			actionsHtml += actionButton(T.remove, 'btn-danger action-del', function() {
				unlink(name, true);
			});
		}
		html += '<div class="' + cardClass + '">';
		html += '<div class="file-card-main">';
		html += renderMobileFolderNav(
			name,
			name === '..' ? 'up' : 'folder',
			nextPath,
			name !== '..' ? '<div class="file-card-meta">' + formatDate(item) + '</div>' : ''
		);
		html += '</div>';
		html += renderMobileCardActions(actionsHtml);
		html += '</div>';
	});

	items.forEach(function(item) {
		if (item.isdir !== 0) {
			return;
		}
		hasRows = true;
		var name = item.fn;
		var fullPath = joinPath(dirPath, name);
		var actionsHtml = actionButton(T.download, 'btn-secondary action-dl', function() {
			downloadFile(fullPath, name);
		});
		actionsHtml += '<span class="action-extra">' + getSpecialAction(name, fullPath) + '</span>';
		actionsHtml += actionButton(T.remove, 'btn-danger action-del', function() {
			unlink(name, false);
		});
		html += '<div class="' + renderMobileCardClass(name, false) + ' file-card-file">';
		html += '<div class="file-card-main"><span class="file-name">' + fileIcon('file') + escapeHtml(name) + '</span>';
		html += '<div class="file-card-meta">' + formatSize(item.sz) + ' \u00b7 ' + formatDate(item) + '</div>';
		html += '</div>';
		html += renderMobileCardActions(actionsHtml);
		html += '</div>';
	});

	if (!hasRows) {
		return '<div class="empty-state">' + T.emptyDir + '</div>';
	}
	html += '</div>';
	return html;
}

function fileIcon(kind) {
	var cls = 'file-icon ico-' + kind;
	if (kind === 'folder') {
		return '<span class="' + cls + '"><svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path fill="currentColor" d="M10 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"/></svg></span>';
	}
	if (kind === 'up') {
		return '<span class="' + cls + '"><svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path fill="currentColor" d="M10 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"/><path fill="currentColor" d="M12 11l-4 4h2.5V18h3v-3H16z"/></svg></span>';
	}
	return '<span class="' + cls + '"><svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path fill="currentColor" d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8l-6-6zm-1 2l5 5h-5V4z"/></svg></span>';
}

// Legacy CP866 table from original 3ws (866 display charset)
var LEGACY_CP866_UNICODE = [
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
	0x0401, 0x0451, 0x0404, 0x0454, 0x0407, 0x0457, 0x040E, 0x045E, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x2116, 0x00A4, 0x25A0, 0x00A0
];
var legacyCyr866 = '';
for (var li = 0; li < LEGACY_CP866_UNICODE.length; li++) {
	legacyCyr866 += String.fromCharCode(LEGACY_CP866_UNICODE[li]);
}

function appendCp866Byte(out, b) {
	var hex = b.toString(16).toUpperCase();
	if (hex.length < 2) {
		hex = '0' + hex;
	}
	out.push('%', hex);
}

function legacyIdxToCp866Byte(idx) {
	if (idx < 80) {
		return idx + 128;
	}
	return idx + 144;
}

function legacyTo866(str) {
	var out = [];
	for (var i = 0; i < str.length; i++) {
		var ch = str.charAt(i);
		var code = str.charCodeAt(i);
		if (code > 127) {
			var idx = legacyCyr866.indexOf(ch);
			if (idx >= 0) {
				appendCp866Byte(out, legacyIdxToCp866Byte(idx));
			} else {
				out.push('x');
			}
		} else {
			out.push(ch);
		}
	}
	return out.join('');
}

var PLAY_EXT = {
	'.pt3': 'bin/player.com%20/',
	'.pt2': 'bin/player.com%20/',
	'.tfc': 'bin/player.com%20/',
	'.m': 'bin/player.com%20/',
	'.mt3': 'bin/player.com%20/',
	'.et': 'bin/player.com%20/',
	'.etc': 'bin/player.com%20/',
	'.cmp': 'bin/player.com%20/',
	'.tfd': 'bin/player.com%20/',
	'.tfm': 'bin/player.com%20/',
	'.mod': 'bin/modplay.com%20/',
	'.mp3': 'bin/gp.com%20/',
	'.mid': 'bin/gp.com%20/',
	'.ogg': 'bin/gp.com%20/',
	'.aac': 'bin/gp.com%20/',
	'.mdr': 'bin/gp.com%20/',
	'.mwm': 'bin/gp.com%20/'
};
var VIEW_EXT = {
	'.16c': 'bin/view.com%20/',
	'.fnt': 'bin/view.com%20/',
	'.img': 'bin/view.com%20/',
	'.3': 'bin/view.com%20/',
	'.888': 'bin/view.com%20/',
	'.y': 'bin/view.com%20/',
	'.+': 'bin/view.com%20/',
	'.-': 'bin/view.com%20/',
	'.plc': 'bin/view.com%20/',
	'.mc ': 'bin/view.com%20/',
	'.mcx': 'bin/view.com%20/',
	'.grf': 'bin/view.com%20/',
	'.ch$': 'bin/view.com%20/',
	'.mg1': 'bin/view.com%20/',
	'.mg2': 'bin/view.com%20/',
	'.mg4': 'bin/view.com%20/',
	'.mg8': 'bin/view.com%20/',
	'.rm': 'bin/view.com%20/',
	'.mlt': 'bin/view.com%20/',
	'.53c': 'bin/view.com%20/',
	'.zxs': 'bin/view.com%20/',
	'.atr': 'bin/view.com%20/',
	'.scr': 'bin/view.com%20/'
};
var BROWSER_EXT = {
	'.gif': 'bin/browser.com%20/',
	'.jpg': 'bin/browser.com%20/',
	'.png': 'bin/browser.com%20/',
	'.htm': 'bin/browser.com%20/',
	'.svg': 'bin/browser.com%20/',
	'.bmp': 'bin/scratch.com%20/'
};

var STOP_GS_CMD = 'bin/modplay.com';
var STOP_AY_CMD = 'bin/player.com';

function $(id) {
	return document.getElementById(id);
}

function escapeHtml(str) {
	return String(str)
		.replace(/&/g, '&amp;')
		.replace(/</g, '&lt;')
		.replace(/>/g, '&gt;')
		.replace(/"/g, '&quot;');
}

function escapeJs(str) {
	return String(str).replace(/\\/g, '\\\\').replace(/'/g, "\\'");
}

function resetRowActions() {
	rowActions = [];
}

function registerRowAction(handler) {
	rowActions.push(handler);
	return rowActions.length - 1;
}

function runRowAction(index) {
	var handler = rowActions[index];
	if (typeof handler === 'function') {
		handler();
	}
}

function actionButton(label, className, handler) {
	var id = registerRowAction(handler);
	return '<button type="button" class="btn btn-sm ' + className + '" onclick="runRowAction(' + id + ')">' + label + '</button>';
}

function actionLink(text, handler) {
	var id = registerRowAction(handler);
	return '<a href="javascript:void(0)" onclick="runRowAction(' + id + ')">' + escapeHtml(text) + '</a>';
}

function joinPath(base, name) {
	if (!base) {
		return name;
	}
	if (!name) {
		return base;
	}
	return base + '/' + name;
}

function normalizeRelativePath(path) {
	return String(path || '').replace(/\\/g, '/').replace(/^\/+/, '');
}

function encodePathArg(path) {
	return encodeURIComponent(path);
}

function prepareApiArg(up, us) {
	if (up === '?d=') {
		return encodeURIComponent(legacyTo866(us));
	}
	return legacyTo866(us);
}

function myGet(up, us) {
	var r = new XMLHttpRequest();
	us = prepareApiArg(up, us);
	r.open('GET', apiUrl(up + us + '&r=' + Math.random()), false);
	try {
		r.setRequestHeader('Connection', 'close');
	} catch (e) {
	}
	r.send(null);
	return r.responseText;
}

function myGetAsync(up, us) {
	return new Promise(function(resolve, reject) {
		var r = new XMLHttpRequest();
		var settled = false;
		us = prepareApiArg(up, us);
		if (downloadJobActive) {
			activeDownloadXhr = r;
		}
		r.open('GET', apiUrl(up + us + '&r=' + Math.random()), true);
		try {
			r.setRequestHeader('Connection', 'close');
		} catch (e) {
		}
		r.timeout = REQUEST_TIMEOUT_MS;
		function finish(err, text) {
			if (settled) {
				return;
			}
			settled = true;
			if (activeDownloadXhr === r) {
				activeDownloadXhr = null;
			}
			try {
				r.abort();
			} catch (e2) {
			}
			if (err) {
				reject(err);
				return;
			}
			resolve(text);
		}
		r.onload = function() {
			if (r.status === 0 || (r.status >= 200 && r.status < 300)) {
				var text = r.responseText;
				if (!text) {
					finish(new Error('Empty response'));
					return;
				}
				finish(null, text);
				return;
			}
			finish(new Error('HTTP ' + r.status));
		};
		r.onerror = function() {
			if (downloadCancelled) {
				finish(new Error('Cancelled'));
				return;
			}
			finish(new Error('Network error'));
		};
		r.onabort = function() {
			if (!settled) {
				finish(new Error('Cancelled'));
			}
		};
		r.ontimeout = function() {
			if (downloadCancelled) {
				finish(new Error('Cancelled'));
				return;
			}
			finish(new Error('Timeout'));
		};
		r.send(null);
	});
}

function sleep(ms) {
	return new Promise(function(resolve, reject) {
		if (downloadCancelled) {
			reject(new Error('Cancelled'));
			return;
		}
		setTimeout(function() {
			if (downloadCancelled) {
				reject(new Error('Cancelled'));
				return;
			}
			resolve();
		}, ms);
	});
}

function readBinaryFromXhr(r) {
	if (!(r.response instanceof ArrayBuffer)) {
		return null;
	}
	return new Uint8Array(r.response);
}

function releaseDownloadXhr(r) {
	if (activeDownloadXhr === r) {
		activeDownloadXhr = null;
	}
}

function validateBinarySize(data, expectedSize, path) {
	var expected = Number(expectedSize);
	if (!expected || !data) {
		return data;
	}
	if (data.length !== expected) {
		throw new Error(T.downloadSizeError + ': ' + data.length + ' / ' + expected + ' (' + path + ')');
	}
	return data;
}

function fetchBinaryOnce(path, expectedSize) {
	return new Promise(function(resolve, reject) {
		var url = apiUrl('?g=' + encodePathArg(path) + '&r=' + Math.random());
		var r = new XMLHttpRequest();
		activeDownloadXhr = r;
		r.open('GET', url, true);
		try {
			r.setRequestHeader('Connection', 'close');
		} catch (e) {
		}
		try {
			r.responseType = 'arraybuffer';
		} catch (e2) {
		}
		r.timeout = REQUEST_TIMEOUT_MS;
		function fail(err) {
			try {
				r.abort();
			} catch (e3) {
			}
			releaseDownloadXhr(r);
			reject(err);
		}
		r.onload = function() {
			if (r.status === 0 || (r.status >= 200 && r.status < 300)) {
				var data = readBinaryFromXhr(r);
				releaseDownloadXhr(r);
				if (!data || !data.length) {
					fail(new Error('Empty response for ' + path));
					return;
				}
				try {
					resolve(validateBinarySize(data, expectedSize, path));
				} catch (sizeErr) {
					fail(sizeErr);
				}
				return;
			}
			fail(new Error('HTTP ' + r.status + ' for ' + path));
		};
		r.onerror = function() {
			if (downloadCancelled) {
				fail(new Error('Cancelled'));
				return;
			}
			fail(new Error('Network error for ' + path));
		};
		r.onabort = function() {
			fail(new Error('Cancelled'));
		};
		r.ontimeout = function() {
			if (downloadCancelled) {
				fail(new Error('Cancelled'));
				return;
			}
			fail(new Error('Timeout for ' + path));
		};
		r.send(null);
	});
}

function fetchBinaryWithRetry(path, attempt, expectedSize) {
	return fetchBinaryOnce(path, expectedSize).catch(function(err) {
		if (downloadCancelled) {
			throw err;
		}
		if (err.message && err.message.indexOf(T.downloadSizeError) === 0) {
			throw err;
		}
		if (attempt + 1 >= DOWNLOAD_MAX_RETRIES) {
			throw err;
		}
		var delay = DOWNLOAD_RETRY_BASE_MS + attempt * DOWNLOAD_RETRY_STEP_MS;
		return sleep(delay).then(function() {
			if (downloadCancelled) {
				throw new Error('Cancelled');
			}
			return fetchBinaryWithRetry(path, attempt + 1, expectedSize);
		});
	});
}

function fetchBinaryAsync(path, expectedSize) {
	return sleep(DOWNLOAD_GAP_MS).then(function() {
		if (downloadCancelled) {
			return Promise.reject(new Error('Cancelled'));
		}
		return fetchBinaryWithRetry(path, 0, expectedSize);
	});
}

function updateStatusPanelVisibility() {
	var panel = $('statusPanel');
	var logEl = $('log');
	var progressEl = $('progressWrap');
	if (!panel) {
		return;
	}
	var hasLog = logEl && String(logEl.textContent || '').trim().length > 0;
	var hasProgress = progressEl && progressEl.classList.contains('active');
	panel.classList.toggle('status-panel-empty', !hasLog && !hasProgress);
}

function clearLog() {
	var el = $('log');
	el.innerHTML = '';
	el.className = 'status';
	updateStatusPanelVisibility();
}

function beginCommand(message) {
	clearLog();
	if (message) {
		var el = $('log');
		el.innerHTML = message;
		el.className = 'status';
	}
	updateStatusPanelVisibility();
}

function log(html, kind) {
	var el = $('log');
	el.innerHTML = html;
	el.className = 'status' + (kind ? ' ' + kind : '');
	updateStatusPanelVisibility();
}

function runAfterPaint(fn) {
	setTimeout(fn, 0);
}

function updateUiBusyState() {
	var app = $('app');
	if (!app) {
		return;
	}
	var busy = downloadJobActive || uploadJobActive || deleteJobActive || readDirBusy;
	app.classList.toggle('app-busy', busy);
}

function setProgress(visible, label, percent) {
	var wrap = $('progressWrap');
	var bar = $('progressBar');
	wrap.classList.toggle('active', !!visible);
	$('progressLabel').textContent = label || '';
	bar.style.width = Math.max(0, Math.min(100, percent || 0)) + '%';
	updateCancelButton(!!visible && (downloadJobActive || uploadJobActive));
	updateStatusPanelVisibility();
}

function updateCancelButton(visible) {
	var btn = $('btnCancelProgress');
	if (btn) {
		btn.style.display = visible ? 'inline-flex' : 'none';
	}
}

function beginDownloadJob() {
	downloadCancelled = false;
	downloadJobActive = true;
	updateCancelButton(true);
	updateUiBusyState();
}

function endDownloadJob() {
	downloadJobActive = false;
	downloadCancelled = false;
	activeDownloadXhr = null;
	updateCancelButton(uploadJobActive);
	updateUiBusyState();
}

function beginUploadJob() {
	uploadCancelled = false;
	uploadJobActive = true;
	updateCancelButton(true);
	updateUiBusyState();
}

function endUploadJob() {
	uploadJobActive = false;
	uploadCancelled = false;
	activeUploadXhr = null;
	uploadActive = false;
	updateCancelButton(downloadJobActive);
	updateUiBusyState();
}

function beginDeleteJob() {
	deleteJobActive = true;
	updateUiBusyState();
}

function endDeleteJob() {
	deleteJobActive = false;
	updateUiBusyState();
}

function resetUploadBatchState() {
	uploadTotalCount = 0;
	uploadDoneCount = 0;
	uploadBatchTotalBytes = 0;
	uploadBatchDoneBytes = 0;
}

function cancelProgressJob() {
	if (downloadJobActive) {
		cancelDownloadJob();
		return;
	}
	if (uploadJobActive) {
		cancelUploadJob();
	}
}

function cancelDownloadJob() {
	if (!downloadJobActive) {
		return;
	}
	downloadCancelled = true;
	setProgress(false);
	if (activeDownloadXhr) {
		try {
			activeDownloadXhr.abort();
		} catch (e) {
		}
		return;
	}
	endDownloadJob();
	log(T.downloadCancelled, '');
}

function cancelUploadJob() {
	if (!uploadJobActive) {
		return;
	}
	uploadCancelled = true;
	uploadQueue = [];
	setProgress(false);
	if (activeUploadXhr) {
		try {
			activeUploadXhr.abort();
		} catch (e) {
		}
	}
	resetUploadBatchState();
	endUploadJob();
	log(T.uploadCancelled, '');
}

function handleDownloadZipError(err) {
	var wasCancelled = downloadCancelled || (err && err.message === 'Cancelled');
	endDownloadJob();
	setProgress(false);
	if (wasCancelled) {
		log(T.downloadCancelled, '');
		return;
	}
	log(T.downloadError + ': ' + escapeHtml(err.message), 'error');
}

function initUI() {
	$('btnRefresh').textContent = T.refresh;
	$('btnMuteGs').textContent = T.muteGs;
	$('btnMuteAy').textContent = T.muteAy;
	$('dirName').placeholder = T.dirPlaceholder;
	$('btnMkdir').textContent = T.mkdir;
	$('btnUploadFiles').textContent = T.uploadFiles;
	$('btnUploadFolder').textContent = T.uploadFolder;
	$('dropzone').textContent = T.dropHint;
	$('btnDownloadSelected').textContent = T.downloadSelected;
	$('btnDeleteSelected').textContent = T.deleteSelected;
	$('btnClearSelection').textContent = T.clearSelection;
	$('btnCancelProgress').textContent = T.cancel;
	updateSelectionBar();
}

function selectionKey(name, isDir) {
	return (isDir ? 'd:' : 'f:') + name;
}

function isItemSelected(name, isDir) {
	return !!selectedItems[selectionKey(name, isDir)];
}

function getSelectedList() {
	var list = [];
	for (var key in selectedItems) {
		if (selectedItems.hasOwnProperty(key)) {
			list.push(selectedItems[key]);
		}
	}
	return list;
}

function getSelectedCount() {
	return getSelectedList().length;
}

function setItemSelected(name, isDir, selected) {
	var key = selectionKey(name, isDir);
	if (selected) {
		selectedItems[key] = { name: name, isDir: isDir };
	} else {
		delete selectedItems[key];
	}
}

function clearSelection() {
	selectedItems = {};
	updateSelectionBar();
	var boxes = document.querySelectorAll('.row-select');
	for (var i = 0; i < boxes.length; i++) {
		boxes[i].checked = false;
		var row = boxes[i].closest('tr');
		if (row) {
			row.classList.remove('selected');
		}
	}
	var selectAll = $('selectAll');
	if (selectAll) {
		selectAll.checked = false;
	}
}

function updateRowActionsState() {
	var hasSelection = getSelectedCount() > 0;
	var table = document.querySelector('table.files');
	if (table) {
		table.classList.toggle('selection-mode', hasSelection);
	}
	var actionBtns = document.querySelectorAll('.actions-cell .btn');
	for (var i = 0; i < actionBtns.length; i++) {
		var el = actionBtns[i];
		if (el.tagName === 'BUTTON') {
			el.disabled = hasSelection;
			continue;
		}
		if (el.tagName === 'A') {
			if (hasSelection) {
				if (!el.hasAttribute('data-href')) {
					el.setAttribute('data-href', el.getAttribute('href') || '');
				}
				el.removeAttribute('href');
				el.setAttribute('aria-disabled', 'true');
				el.classList.add('disabled');
			} else {
				var href = el.getAttribute('data-href');
				if (href) {
					el.setAttribute('href', href);
				}
				el.removeAttribute('aria-disabled');
				el.classList.remove('disabled');
			}
		}
	}
}

function updateSelectionBar() {
	var count = getSelectedCount();
	var bar = $('selectionBar');
	bar.classList.toggle('active', count > 0);
	$('selectionCount').textContent = String(count);
	$('selectionLabel').textContent = T.selectedLabel;
	$('btnDownloadSelected').disabled = count === 0;
	$('btnDeleteSelected').disabled = count === 0;
	$('btnClearSelection').disabled = count === 0;
	updateRowActionsState();
}

function renderSelectCell(name, isDir) {
	var checked = isItemSelected(name, isDir) ? ' checked' : '';
	return '<td class="select-cell"><input type="checkbox" class="row-select" data-name="' + escapeHtml(name) + '" data-isdir="' + (isDir ? '1' : '0') + '"' + checked + '></td>';
}

function renderRowClass(name, isDir) {
	return isItemSelected(name, isDir) ? ' class="selected"' : '';
}

function allSelectableSelected() {
	var boxes = document.querySelectorAll('.row-select');
	if (!boxes.length) {
		return false;
	}
	for (var i = 0; i < boxes.length; i++) {
		if (!boxes[i].checked) {
			return false;
		}
	}
	return true;
}

function bindTableSelection() {
	if (isMobileUi()) {
		updateSelectionBar();
		return;
	}
	var selectAll = $('selectAll');
	if (selectAll) {
		selectAll.checked = allSelectableSelected();
		selectAll.onchange = function() {
			var boxes = document.querySelectorAll('.row-select');
			for (var i = 0; i < boxes.length; i++) {
				var name = boxes[i].getAttribute('data-name');
				var isDir = boxes[i].getAttribute('data-isdir') === '1';
				boxes[i].checked = selectAll.checked;
				setItemSelected(name, isDir, selectAll.checked);
				var row = boxes[i].closest('tr');
				if (row) {
					row.classList.toggle('selected', selectAll.checked);
				}
			}
			updateSelectionBar();
		};
	}
	var boxes = document.querySelectorAll('.row-select');
	for (var j = 0; j < boxes.length; j++) {
		(function(box) {
			var name = box.getAttribute('data-name');
			var isDir = box.getAttribute('data-isdir') === '1';
			box.onchange = function() {
				setItemSelected(name, isDir, box.checked);
				var row = box.closest('tr');
				if (row) {
					row.classList.toggle('selected', box.checked);
				}
				if (selectAll) {
					selectAll.checked = allSelectableSelected();
				}
				updateSelectionBar();
			};
		})(boxes[j]);
	}
	updateSelectionBar();
}

function buildZipEntryListAsync(basePath, zipPrefix, onProgress) {
	if (downloadCancelled) {
		return Promise.reject(new Error('Cancelled'));
	}
	return myGetAsync('?d=', basePath).then(function(text) {
		if (downloadCancelled) {
			return Promise.reject(new Error('Cancelled'));
		}
		var data;
		try {
			data = JSON.parse(text);
		} catch (e) {
			return Promise.reject(new Error('Invalid directory listing'));
		}
		if (!data || !data.fno) {
			return Promise.reject(new Error('Invalid directory listing'));
		}
		data.fno.sort(compareFileInfo);
		var list = [];
		var chain = Promise.resolve();
		for (var i = 0; i < data.fno.length; i++) {
			(function(item) {
				chain = chain.then(function() {
					if (downloadCancelled) {
						return Promise.reject(new Error('Cancelled'));
					}
					if (item.fn === '.' || item.fn === '..') {
						return;
					}
					var childPath = joinPath(basePath, item.fn);
					var childZip = zipPrefix ? zipPrefix + '/' + item.fn : item.fn;
					if (item.isdir === 1) {
						return buildZipEntryListAsync(childPath, childZip, onProgress).then(function(sub) {
							for (var j = 0; j < sub.length; j++) {
								list.push(sub[j]);
							}
							if (onProgress) {
								onProgress(list.length);
							}
						});
					}
					if (item.isdir === 0) {
						list.push({ path: childPath, zipName: childZip, size: item.sz });
						if (onProgress) {
							onProgress(list.length);
						}
					}
				});
			})(data.fno[i]);
		}
		return chain.then(function() {
			if (downloadCancelled) {
				return Promise.reject(new Error('Cancelled'));
			}
			return list;
		});
	});
}

function buildSelectedZipEntryList(items, onProgress) {
	var chain = Promise.resolve([]);
	for (var i = 0; i < items.length; i++) {
		(function(item) {
			chain = chain.then(function(fileList) {
				if (downloadCancelled) {
					return Promise.reject(new Error('Cancelled'));
				}
				if (item.isDir) {
					return buildZipEntryListAsync(joinPath(curDir, item.name), item.name, onProgress).then(function(sub) {
						return fileList.concat(sub);
					});
				}
				fileList.push({
					path: joinPath(curDir, item.name),
					zipName: item.name
				});
				if (onProgress) {
					onProgress(fileList.length);
				}
				return fileList;
			});
		})(items[i]);
	}
	return chain;
}

function downloadZipEntries(fileList, onProgress) {
	var entries = [];
	var chain = Promise.resolve();
	for (var i = 0; i < fileList.length; i++) {
		(function(entry) {
			chain = chain.then(function() {
				if (downloadCancelled) {
					return Promise.reject(new Error('Cancelled'));
				}
				return fetchBinaryAsync(entry.path, entry.size).then(function(dataBytes) {
					if (downloadCancelled) {
						return Promise.reject(new Error('Cancelled'));
					}
					entries.push({ name: entry.zipName, data: dataBytes });
					if (onProgress) {
						onProgress(entries.length, fileList.length);
					}
				});
			});
		})(fileList[i]);
	}
	return chain.then(function() {
		if (downloadCancelled) {
			return Promise.reject(new Error('Cancelled'));
		}
		return entries;
	});
}

function runDownloadZip(fileList, zipName, progressMax) {
	setProgress(true, T.collectFiles + '...', 0);
	downloadZipEntries(fileList, function(done, total) {
		setProgress(true, T.collectedFiles + ': ' + done + ' / ' + total, Math.min(progressMax, Math.round((done / total) * progressMax)));
	})
		.then(function(entries) {
			if (downloadCancelled) {
				return Promise.reject(new Error('Cancelled'));
			}
			setProgress(true, T.creatingZip + '...', 96);
			var zipBytes = createZip(entries);
			var blob = new Blob([zipBytes], { type: 'application/zip' });
			triggerDownload(blob, zipName);
			endDownloadJob();
			setProgress(false);
			log(T.zipReady + ' "' + escapeHtml(zipName) + '" (' + entries.length + ' ' + T.filesWord + ')', 'success');
		})
		.catch(handleDownloadZipError);
}

function downloadSelected() {
	var items = getSelectedList();
	if (!items.length) {
		beginCommand('');
		log(T.nothingSelected, 'error');
		return;
	}
	beginCommand(T.zipPrepare + '...');
	beginDownloadJob();
	setProgress(true, T.collectFiles + '...', 0);
	buildSelectedZipEntryList(items, function(count) {
		setProgress(true, T.collectFiles + ': ' + count, Math.min(5, count));
	})
		.then(function(fileList) {
			if (downloadCancelled) {
				return Promise.reject(new Error('Cancelled'));
			}
			if (!fileList.length) {
				endDownloadJob();
				setProgress(false);
				log(T.emptyDownload, 'error');
				return;
			}
			runDownloadZip(fileList, T.selectedZipName, 90);
		})
		.catch(handleDownloadZipError);
}

function deleteSelected() {
	var items = getSelectedList();
	if (!items.length) {
		beginCommand('');
		log(T.nothingSelected, 'error');
		return;
	}
	if (!confirm(T.deleteSelectedConfirm + ' (' + items.length + ')?')) {
		return;
	}
	beginCommand(T.deleteProgress + '...');
	beginDeleteJob();
	setProgress(true, T.deleteProgress + '...', 0);
	Promise.all(items.map(countDeleteTarget))
		.then(function(counts) {
			var total = 0;
			for (var c = 0; c < counts.length; c++) {
				total += counts[c];
			}
			beginDeleteBatch(total);
			var chain = Promise.resolve();
			for (var i = 0; i < items.length; i++) {
				(function(item) {
					chain = chain.then(function() {
						var fullPath = joinPath(curDir, item.name);
						if (item.isDir) {
							return deleteFolderRecursive(fullPath, updateDeleteProgress);
						}
						updateDeleteProgress(fullPath);
						return unlinkPathAsync(fullPath);
					});
				})(items[i]);
			}
			return chain;
		})
		.then(function() {
			finishDeleteProgress(T.deleteSelectedDone);
			clearSelection();
			log(T.deleteSelectedDone, 'success');
			rddir(curDir);
		})
		.catch(function(err) {
			deleteTotalCount = 0;
			deleteDoneCount = 0;
			setProgress(false);
			endDeleteJob();
			log(T.deleteError + ': ' + escapeHtml(err.message), 'error');
			rddir(curDir);
		});
}

function bindSelectionBar() {
	$('btnDownloadSelected').onclick = downloadSelected;
	$('btnDeleteSelected').onclick = deleteSelected;
	$('btnClearSelection').onclick = clearSelection;
}

function stopDaemon() {
	beginCommand(T.stoppingDaemon + '...');
	runAfterPaint(function() {
		log(myGet('?x=1', ''), '');
	});
}

function mkdir() {
	var name = $('dirName').value.replace(/[/\\]/g, '').trim();
	if (!name) {
		beginCommand('');
		log(T.enterDirName, 'error');
		return;
	}
	beginCommand(T.mkdirProgress + '...');
	runAfterPaint(function() {
		var result = myGet('?m=', joinPath(curDir, name));
		$('dirName').value = '';
		if (result) {
			log(result, 'success');
		}
		rddir(curDir);
	});
}

function unlinkPathAsync(path) {
	return myGetAsync('?u=', path);
}

function beginDeleteBatch(total) {
	deleteTotalCount = total;
	deleteDoneCount = 0;
}

function updateDeleteProgress(path) {
	deleteDoneCount++;
	var percent = deleteTotalCount > 0
		? Math.round((deleteDoneCount / deleteTotalCount) * 100)
		: 100;
	var label = T.deleteProgress + ': ' + deleteDoneCount + ' / ' + deleteTotalCount;
	if (path) {
		label += ' \u2014 ' + uploadDisplayName(path);
	}
	setProgress(true, label, percent);
}

function finishDeleteProgress(successLabel) {
	if (deleteTotalCount > 0) {
		setProgress(true, successLabel || T.deleteProgress, 100);
	}
	deleteTotalCount = 0;
	deleteDoneCount = 0;
	setTimeout(function() {
		setProgress(false);
		endDeleteJob();
	}, 400);
}

function countFolderDeleteItems(folderPath) {
	return myGetAsync('?d=', folderPath).then(function(text) {
		var data = JSON.parse(text);
		var total = 1;
		var nested = [];
		for (var i = 0; i < data.fno.length; i++) {
			var item = data.fno[i];
			if (item.fn === '.' || item.fn === '..') {
				continue;
			}
			var childPath = joinPath(folderPath, item.fn);
			if (item.isdir === 1) {
				nested.push(countFolderDeleteItems(childPath));
			} else if (item.isdir === 0) {
				total++;
			}
		}
		return Promise.all(nested).then(function(counts) {
			for (var j = 0; j < counts.length; j++) {
				total += counts[j];
			}
			return total;
		});
	});
}

function countDeleteTarget(item) {
	var fullPath = joinPath(curDir, item.name);
	if (item.isDir) {
		return countFolderDeleteItems(fullPath);
	}
	return Promise.resolve(1);
}

function deleteFolderRecursive(folderPath, onProgress) {
	return myGetAsync('?d=', folderPath).then(function(text) {
		var data = JSON.parse(text);
		var chain = Promise.resolve();
		for (var i = 0; i < data.fno.length; i++) {
			(function(item) {
				if (item.fn === '.' || item.fn === '..') {
					return;
				}
				var childPath = joinPath(folderPath, item.fn);
				chain = chain.then(function() {
					if (item.isdir === 1) {
						return deleteFolderRecursive(childPath, onProgress);
					}
					if (item.isdir === 0) {
						if (onProgress) {
							onProgress(childPath);
						}
						return unlinkPathAsync(childPath);
					}
				});
			})(data.fno[i]);
		}
		return chain.then(function() {
			if (onProgress) {
				onProgress(folderPath);
			}
			return unlinkPathAsync(folderPath);
		});
	});
}

function unlink(dirPath, isDir) {
	var fullPath = joinPath(curDir, dirPath);
	if (isDir) {
		if (!confirm(T.deleteConfirmFolder + ' "' + dirPath + '"?')) {
			return;
		}
		beginCommand(T.deletingFolder + ' "' + escapeHtml(dirPath) + '"...');
		beginDeleteJob();
		setProgress(true, T.deleteProgress + '...', 0);
		countFolderDeleteItems(fullPath)
			.then(function(total) {
				beginDeleteBatch(total);
				return deleteFolderRecursive(fullPath, updateDeleteProgress);
			})
			.then(function() {
				finishDeleteProgress(T.deleteFolderDone);
				log(T.deleteFolderDone + ' "' + escapeHtml(dirPath) + '"', 'success');
				rddir(curDir);
			})
			.catch(function(err) {
				deleteTotalCount = 0;
				deleteDoneCount = 0;
				setProgress(false);
				endDeleteJob();
				log(T.deleteError + ': ' + escapeHtml(err.message), 'error');
				rddir(curDir);
			});
		return;
	}
	if (!confirm(T.deleteConfirm + ' ' + T.deleteFile + ' "' + dirPath + '"?')) {
		return;
	}
	beginCommand(T.deletingFile + ' "' + escapeHtml(dirPath) + '"...');
	runAfterPaint(function() {
		log(myGet('?u=', fullPath), 'success');
		rddir(curDir);
	});
}

function runprog(command) {
	beginCommand(T.running + '...');
	runAfterPaint(function() {
		log(myGet('?s=', command), '');
	});
}

function muteGs() {
	beginCommand(T.mutingGs);
	runAfterPaint(function() {
		log(myGet('?s=', STOP_GS_CMD), '');
	});
}

function muteAy() {
	beginCommand(T.mutingAy);
	runAfterPaint(function() {
		log(myGet('?s=', STOP_AY_CMD), '');
	});
}

function compareFileInfo(finfoA, finfoB) {
	if (finfoA.isdir === 3 || finfoB.isdir === 3) {
		return 0;
	}
	return finfoA.fn.localeCompare(finfoB.fn);
}

function formatDate(item) {
	var dt = new Date(
		(item.dt >> 9) + 1980,
		(item.dt >> 5) % 16 - 1,
		item.dt % 32,
		(item.tm >> 11) % 32,
		(item.tm >> 5) % 64,
		(item.tm % 32) * 2,
		0
	);
	return dt.toLocaleString(uiLang === 'ru' ? 'ru' : 'en', {
		year: 'numeric',
		month: 'numeric',
		day: 'numeric',
		hour: 'numeric',
		minute: 'numeric'
	});
}

function formatSize(bytes) {
	if (bytes === '' || bytes === undefined || bytes === null) {
		return T.dash;
	}
	var n = Number(bytes);
	if (n < 1024) {
		return n + ' B';
	}
	if (n < 1048576) {
		return (n / 1024).toFixed(1) + ' KB';
	}
	return (n / 1048576).toFixed(1) + ' MB';
}

function getParentPath(dirPath) {
	if (!dirPath) {
		return '';
	}
	var idx = dirPath.lastIndexOf('/');
	return idx >= 0 ? dirPath.substring(0, idx) : '';
}

function getSpecialAction(name, fullPath) {
	var dot = name.lastIndexOf('.');
	if (dot === -1) {
		return '';
	}
	var ext = name.toLowerCase().substring(dot);
	if (ext === '.com') {
		return actionButton(T.run, 'btn-secondary action-run', function() {
			runprog(fullPath);
		});
	}
	if (PLAY_EXT[ext]) {
		return actionButton(T.play, 'btn-secondary action-run', function() {
			runprog(PLAY_EXT[ext] + fullPath);
		});
	}
	if (VIEW_EXT[ext]) {
		return actionButton(T.view, 'btn-secondary action-run', function() {
			runprog(VIEW_EXT[ext] + fullPath);
		});
	}
	if (BROWSER_EXT[ext]) {
		return actionButton(T.view, 'btn-secondary action-run', function() {
			runprog(BROWSER_EXT[ext] + fullPath);
		});
	}
	return '';
}

function renderBreadcrumbs(dirPath) {
	var html = '';
	if (!dirPath) {
		html = '<span class="current">0:</span>';
	} else {
		html = '<a href="javascript:rddir(\'\')">0:</a>';
		var parts = dirPath.split('/');
		var acc = '';
		for (var i = 0; i < parts.length; i++) {
			html += '<span class="sep">/</span>';
			acc = acc ? acc + '/' + parts[i] : parts[i];
			if (i === parts.length - 1) {
				html += '<span class="current">' + escapeHtml(parts[i]) + '</span>';
			} else {
				html += '<a href="javascript:rddir(\'' + escapeJs(acc) + '\')">' + escapeHtml(parts[i]) + '</a>';
			}
		}
	}
	$('breadcrumbs').innerHTML = html;
}

function renderDirectoryTable(items, dirPath) {
	resetRowActions();
	if (isMobileUi()) {
		return renderMobileDirectory(items, dirPath);
	}
	var html = '<table class="files"><thead><tr><th class="select-cell"><input type="checkbox" id="selectAll"></th><th>' + T.colName + '</th><th>' + T.colSize + '</th><th>' + T.colDate + '</th><th class="actions-cell">' + T.colActions + '</th></tr></thead><tbody>';
	var hasRows = false;

	items.forEach(function(item) {
		if (item.isdir !== 1 || item.fn === '.') {
			return;
		}
		hasRows = true;
		var name = item.fn;
		var nextPath = name === '..' ? getParentPath(dirPath) : joinPath(dirPath, name);
		html += '<tr' + (name !== '..' ? renderRowClass(name, true) : '') + '>';
		if (name !== '..') {
			html += renderSelectCell(name, true);
		} else {
			html += '<td class="select-cell"></td>';
		}
		html += '<td><span class="file-name">' + fileIcon(name === '..' ? 'up' : 'folder');
		html += actionLink(name, function() {
			rddir(nextPath);
		}) + '</span></td>';
		html += '<td>' + T.dash + '</td><td>' + (name === '..' ? T.dash : formatDate(item)) + '</td><td class="actions-cell"><div class="actions">';
		if (name !== '..') {
			html += '<span class="action-extra"></span>';
			html += actionButton(T.downloadZip, 'btn-secondary action-dl', function() {
				downloadFolder(name);
			});
			html += actionButton(T.remove, 'btn-danger action-del', function() {
				unlink(name, true);
			});
		}
		html += '</div></td></tr>';
	});

	items.forEach(function(item) {
		if (item.isdir !== 0) {
			return;
		}
		hasRows = true;
		var name = item.fn;
		var fullPath = joinPath(dirPath, name);
		html += '<tr' + renderRowClass(name, false) + '>';
		html += renderSelectCell(name, false);
		html += '<td><span class="file-name">' + fileIcon('file') + escapeHtml(name) + '</span></td>';
		html += '<td>' + formatSize(item.sz) + '</td>';
		html += '<td>' + formatDate(item) + '</td><td class="actions-cell"><div class="actions">';
		html += '<span class="action-extra">' + getSpecialAction(name, fullPath) + '</span>';
		html += actionButton(T.download, 'btn-secondary action-dl', function() {
			downloadFile(fullPath, name);
		});
		html += actionButton(T.remove, 'btn-danger action-del', function() {
			unlink(name, false);
		});
		html += '</div></td></tr>';
	});

	if (!hasRows) {
		return '<div class="empty-state">' + T.emptyDir + '</div>';
	}
	html += '</tbody></table>';
	return html;
}

function rddir(dirPath) {
	if (dirPath !== lastListDir) {
		clearSelection();
	}
	lastListDir = dirPath;
	curDir = dirPath;
	renderBreadcrumbs(dirPath);
	beginCommand(T.readingDir + '...');
	readDirBusy = true;
	updateUiBusyState();
	try {
		var data = JSON.parse(myGet('?d=', dirPath));
		data.fno.sort(compareFileInfo);
		$('divlog').innerHTML = renderDirectoryTable(data.fno, dirPath);
		bindTableSelection();
		clearLog();
	} catch (e) {
		$('divlog').innerHTML = '<div class="empty-state">' + T.readDirError + '</div>';
		log(T.dirError, 'error');
		updateSelectionBar();
	} finally {
		readDirBusy = false;
		updateUiBusyState();
	}
}

function ensureDirectory(path) {
	if (!path) {
		return;
	}
	var parts = path.split('/');
	var acc = '';
	for (var i = 0; i < parts.length; i++) {
		if (!parts[i]) {
			continue;
		}
		acc = acc ? acc + '/' + parts[i] : parts[i];
		myGet('?m=', acc);
	}
}

function refreshAfterUpload() {
	var dir = uploadCurDir;
	rddir(dir);
	setTimeout(function() {
		rddir(dir);
	}, 250);
}

function uploadDisplayName(relativePath) {
	var idx = relativePath.lastIndexOf('/');
	return idx >= 0 ? relativePath.substring(idx + 1) : relativePath;
}

function updateUploadProgress(relativePath, fileLoaded, fileTotal) {
	var percent = 0;
	if (uploadBatchTotalBytes > 0) {
		percent = Math.round(((uploadBatchDoneBytes + fileLoaded) / uploadBatchTotalBytes) * 100);
	} else if (uploadTotalCount > 0) {
		var filePart = fileTotal > 0 ? fileLoaded / fileTotal : 0;
		percent = Math.round(((uploadDoneCount + filePart) / uploadTotalCount) * 100);
	}
	var label = T.uploadProgress + ': ' + (uploadDoneCount + 1) + ' / ' + uploadTotalCount;
	if (relativePath) {
		label += ' \u2014 ' + uploadDisplayName(relativePath);
	}
	setProgress(true, label, percent);
}

function beginUploadBatch(queue) {
	uploadTotalCount = queue.length;
	uploadDoneCount = 0;
	uploadBatchTotalBytes = 0;
	uploadBatchDoneBytes = 0;
	for (var i = 0; i < queue.length; i++) {
		uploadBatchTotalBytes += queue[i].file.size || 0;
	}
}

function finishUploadFile(file) {
	uploadDoneCount++;
	uploadBatchDoneBytes += file.size || 0;
}

function putFileAsync(relativePath, file) {
	return new Promise(function(resolve, reject) {
		if (uploadCancelled) {
			reject(new Error('Cancelled'));
			return;
		}
		var target = joinPath(uploadCurDir, normalizeRelativePath(relativePath));
		var parent = target.lastIndexOf('/') >= 0 ? target.substring(0, target.lastIndexOf('/')) : '';
		if (parent) {
			ensureDirectory(parent);
		}
		var xhr = new XMLHttpRequest();
		activeUploadXhr = xhr;
		var settled = false;
		var emptyWaits = 0;
		function releaseUploadXhr() {
			if (activeUploadXhr === xhr) {
				activeUploadXhr = null;
			}
		}
		function doneOk() {
			if (settled) {
				return;
			}
			settled = true;
			releaseUploadXhr();
			finishUploadFile(file);
			resolve();
		}
		function doneErr(msg) {
			if (settled) {
				return;
			}
			settled = true;
			releaseUploadXhr();
			reject(new Error(msg));
		}
		function waitResponse() {
			if (uploadCancelled) {
				doneErr('Cancelled');
				return;
			}
			if (xhr.readyState !== 4) {
				setTimeout(waitResponse, 100);
				return;
			}
			if (xhr.status !== 0 && (xhr.status < 200 || xhr.status >= 300)) {
				doneErr('HTTP ' + xhr.status + ' for ' + relativePath);
				return;
			}
			if (xhr.responseText === '') {
				emptyWaits++;
				if (emptyWaits > 50) {
					doneOk();
					return;
				}
				setTimeout(waitResponse, 100);
				return;
			}
			doneOk();
		}
		xhr.open('PUT', apiUrl(legacyTo866(target)), true);
		xhr.timeout = REQUEST_TIMEOUT_MS;
		updateUploadProgress(relativePath, 0, file.size || 0);
		xhr.upload.onprogress = function(event) {
			if (uploadCancelled) {
				return;
			}
			var total = event.lengthComputable ? event.total : (file.size || 0);
			updateUploadProgress(relativePath, event.loaded || 0, total);
		};
		xhr.upload.onload = waitResponse;
		xhr.upload.onerror = function() {
			if (uploadCancelled) {
				doneErr('Cancelled');
				return;
			}
			doneErr('Network error for ' + relativePath);
		};
		xhr.onreadystatechange = waitResponse;
		xhr.onerror = function() {
			if (uploadCancelled) {
				doneErr('Cancelled');
				return;
			}
			doneErr('Network error for ' + relativePath);
		};
		xhr.onabort = function() {
			doneErr('Cancelled');
		};
		xhr.send(file);
	});
}

function queueUploadTasks(tasks) {
	uploadCurDir = curDir;
	uploadQueue = [];
	for (var i = 0; i < tasks.length; i++) {
		var relative = normalizeRelativePath(tasks[i].relative);
		if (!relative || relative === '.' || relative === '..') {
			continue;
		}
		uploadQueue.push({ file: tasks[i].file, relative: relative });
	}
	if (!uploadQueue.length) {
		beginCommand('');
		log(T.noUploadFiles, 'error');
		return;
	}
	if (!uploadJobActive) {
		beginUploadJob();
	}
	beginUploadBatch(uploadQueue);
	beginCommand(T.uploadProgress + '...');
	updateUploadProgress('', 0, 0);
	processUploadQueue();
}

function queueUploads(files, preserveRelative) {
	var tasks = [];
	for (var i = 0; i < files.length; i++) {
		var file = files[i];
		tasks.push({
			file: file,
			relative: preserveRelative
				? normalizeRelativePath(file.webkitRelativePath || file.name)
				: file.name
		});
	}
	queueUploadTasks(tasks);
}

function flattenUploadTasks(nested) {
	var out = [];
	for (var i = 0; i < nested.length; i++) {
		out = out.concat(nested[i]);
	}
	return out;
}

function readDirectoryEntries(reader) {
	return new Promise(function(resolve, reject) {
		var entries = [];
		function readBatch() {
			reader.readEntries(function(batch) {
				if (!batch.length) {
					resolve(entries);
					return;
				}
				entries = entries.concat(batch);
				readBatch();
			}, reject);
		}
		readBatch();
	});
}

function entryRelativePath(entry, file) {
	if (entry && entry.fullPath) {
		var path = entry.fullPath;
		if (path.charAt(0) === '/') {
			path = path.slice(1);
		}
		return normalizeRelativePath(path);
	}
	return normalizeRelativePath(file.name);
}

function isDirectoryEntry(entry) {
	if (entry.isDirectory) {
		return true;
	}
	if (entry.isFile) {
		return false;
	}
	return typeof entry.createReader === 'function';
}

function isFileEntry(entry) {
	if (entry.isFile) {
		return true;
	}
	if (entry.isDirectory) {
		return false;
	}
	return typeof entry.file === 'function';
}

function walkDropEntry(entry) {
	if (isDirectoryEntry(entry)) {
		var reader = entry.createReader();
		return readDirectoryEntries(reader).then(function(children) {
			if (!children.length) {
				return [];
			}
			return Promise.all(children.map(walkDropEntry)).then(flattenUploadTasks);
		});
	}
	if (isFileEntry(entry)) {
		return new Promise(function(resolve, reject) {
			entry.file(function(file) {
				resolve([{ file: file, relative: entryRelativePath(entry, file) }]);
			}, reject);
		});
	}
	return Promise.reject(new Error('Unsupported drop item'));
}

function getDropItemEntry(item) {
	if (item.webkitGetAsEntry) {
		return item.webkitGetAsEntry();
	}
	if (item.getAsEntry) {
		return item.getAsEntry();
	}
	return null;
}

function snapshotDropEntries(dataTransfer) {
	var snapshots = [];
	var items = dataTransfer.items;
	if (!items || !items.length) {
		return snapshots;
	}
	for (var i = 0; i < items.length; i++) {
		var item = items[i];
		if (item.kind !== 'file') {
			continue;
		}
		var entry = getDropItemEntry(item);
		if (entry) {
			snapshots.push({ type: 'entry', entry: entry });
			continue;
		}
		if (item.getAsFile) {
			var file = item.getAsFile();
			if (file) {
				snapshots.push({ type: 'file', file: file });
			}
		}
	}
	return snapshots;
}

function snapshotDroppedFiles(files) {
	var snapshots = [];
	if (!files || !files.length) {
		return snapshots;
	}
	var hasRelative = false;
	for (var i = 0; i < files.length; i++) {
		if (files[i].webkitRelativePath && files[i].webkitRelativePath.length) {
			hasRelative = true;
			break;
		}
	}
	for (var j = 0; j < files.length; j++) {
		var f = files[j];
		var rel = hasRelative && f.webkitRelativePath && f.webkitRelativePath.length
			? normalizeRelativePath(f.webkitRelativePath)
			: f.name;
		snapshots.push({ type: 'file', file: f, relative: rel });
	}
	return snapshots;
}

function collectDropUploadTasks(snapshots) {
	if (!snapshots.length) {
		return Promise.resolve([]);
	}
	return Promise.all(snapshots.map(function(snap) {
		if (snap.type === 'entry') {
			return walkDropEntry(snap.entry);
		}
		var f = snap.file;
		return Promise.resolve([{
			file: f,
			relative: snap.relative || f.name
		}]);
	})).then(flattenUploadTasks);
}

function collectAllDropUploadTasks(dataTransfer) {
	var entrySnapshots = snapshotDropEntries(dataTransfer);
	if (entrySnapshots.length) {
		return collectDropUploadTasks(entrySnapshots).then(function(tasks) {
			if (tasks.length) {
				return tasks;
			}
			return collectDropUploadTasks(snapshotDroppedFiles(dataTransfer.files));
		});
	}
	return collectDropUploadTasks(snapshotDroppedFiles(dataTransfer.files));
}

function processUploadQueue() {
	if (uploadActive) {
		return;
	}
	if (uploadCancelled) {
		return;
	}
	if (!uploadQueue.length) {
		if (uploadCancelled) {
			return;
		}
		if (uploadTotalCount > 0) {
			setProgress(true, T.uploadDone, 100);
		}
		resetUploadBatchState();
		setTimeout(function() {
			setProgress(false);
			endUploadJob();
		}, 400);
		log(T.uploadDone, 'success');
		refreshAfterUpload();
		return;
	}
	uploadActive = true;
	var task = uploadQueue.shift();
	putFileAsync(task.relative, task.file)
		.then(function() {
			uploadActive = false;
			processUploadQueue();
		})
		.catch(function(err) {
			uploadActive = false;
			if (uploadCancelled || (err && err.message === 'Cancelled')) {
				return;
			}
			log(T.uploadError + ': ' + escapeHtml(err.message), 'error');
			processUploadQueue();
		});
}

var CRC32_TABLE = (function() {
	var table = new Uint32Array(256);
	for (var i = 0; i < 256; i++) {
		var c = i;
		for (var k = 0; k < 8; k++) {
			c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
		}
		table[i] = c >>> 0;
	}
	return table;
})();

function crc32(data) {
	var crc = 0 ^ (-1);
	for (var i = 0; i < data.length; i++) {
		crc = (crc >>> 8) ^ CRC32_TABLE[(crc ^ data[i]) & 0xFF];
	}
	return (crc ^ (-1)) >>> 0;
}

function writeUint32LE(view, offset, value) {
	view[offset] = value & 0xFF;
	view[offset + 1] = (value >>> 8) & 0xFF;
	view[offset + 2] = (value >>> 16) & 0xFF;
	view[offset + 3] = (value >>> 24) & 0xFF;
}

function writeUint16LE(view, offset, value) {
	view[offset] = value & 0xFF;
	view[offset + 1] = (value >>> 8) & 0xFF;
}

function encodeZipName(name) {
	var bytes = [];
	for (var i = 0; i < name.length; i++) {
		bytes.push(name.charCodeAt(i) & 0xFF);
	}
	return new Uint8Array(bytes);
}

function createZip(entries) {
	var parts = [];
	var central = [];
	var offset = 0;

	for (var i = 0; i < entries.length; i++) {
		var entry = entries[i];
		var nameBytes = encodeZipName(entry.name);
		var data = entry.data;
		var size = data.length;
		var checksum = crc32(data);
		var localHeader = new Uint8Array(30 + nameBytes.length);
		var localView = localHeader;
		writeUint32LE(localView, 0, 0x04034b50);
		writeUint16LE(localView, 4, 20);
		writeUint16LE(localView, 6, 0);
		writeUint16LE(localView, 8, 0);
		writeUint16LE(localView, 10, 0);
		writeUint16LE(localView, 12, 0);
		writeUint32LE(localView, 14, checksum);
		writeUint32LE(localView, 18, size);
		writeUint32LE(localView, 22, size);
		writeUint16LE(localView, 26, nameBytes.length);
		writeUint16LE(localView, 28, 0);
		localHeader.set(nameBytes, 30);

		parts.push(localHeader, data);

		var centralHeader = new Uint8Array(46 + nameBytes.length);
		var centralView = centralHeader;
		writeUint32LE(centralView, 0, 0x02014b50);
		writeUint16LE(centralView, 4, 20);
		writeUint16LE(centralView, 6, 20);
		writeUint16LE(centralView, 8, 0);
		writeUint16LE(centralView, 10, 0);
		writeUint16LE(centralView, 12, 0);
		writeUint16LE(centralView, 14, 0);
		writeUint32LE(centralView, 16, checksum);
		writeUint32LE(centralView, 20, size);
		writeUint32LE(centralView, 24, size);
		writeUint16LE(centralView, 28, nameBytes.length);
		writeUint16LE(centralView, 30, 0);
		writeUint16LE(centralView, 32, 0);
		writeUint16LE(centralView, 34, 0);
		writeUint16LE(centralView, 36, 0);
		writeUint32LE(centralView, 38, 0);
		writeUint32LE(centralView, 42, offset);
		centralHeader.set(nameBytes, 46);
		central.push(centralHeader);

		offset += localHeader.length + data.length;
	}

	var centralSize = 0;
	for (var c = 0; c < central.length; c++) {
		centralSize += central[c].length;
	}
	var end = new Uint8Array(22);
	var endView = end;
	writeUint32LE(endView, 0, 0x06054b50);
	writeUint16LE(endView, 4, 0);
	writeUint16LE(endView, 6, 0);
	writeUint16LE(endView, 8, entries.length);
	writeUint16LE(endView, 10, entries.length);
	writeUint32LE(endView, 12, centralSize);
	writeUint32LE(endView, 16, offset);
	writeUint16LE(endView, 20, 0);

	var totalSize = offset + centralSize + end.length;
	var zip = new Uint8Array(totalSize);
	var pos = 0;
	for (var a = 0; a < parts.length; a++) {
		zip.set(parts[a], pos);
		pos += parts[a].length;
	}
	for (var b = 0; b < central.length; b++) {
		zip.set(central[b], pos);
		pos += central[b].length;
	}
	zip.set(end, pos);
	return zip;
}

function triggerDownload(blob, filename) {
	var url = URL.createObjectURL(blob);
	var link = document.createElement('a');
	link.href = url;
	link.download = filename;
	document.body.appendChild(link);
	link.click();
	document.body.removeChild(link);
	setTimeout(function() {
		URL.revokeObjectURL(url);
	}, 1000);
}

function downloadFile(filePath, fileName) {
	log(T.download + ': ' + escapeHtml(fileName) + '...', '');
	var link = document.createElement('a');
	link.href = apiUrl('?g=' + encodePathArg(filePath) + '&r=' + Math.random());
	link.download = fileName || '';
	document.body.appendChild(link);
	link.click();
	document.body.removeChild(link);
	log(T.download + ': ' + escapeHtml(fileName), 'success');
}

function downloadFolder(folderName) {
	var folderPath = joinPath(curDir, folderName);
	var zipName = folderName + '.zip';
	beginCommand(T.zipPrepare + ' "' + escapeHtml(zipName) + '"...');
	beginDownloadJob();
	setProgress(true, T.collectFiles + '...', 0);
	buildZipEntryListAsync(folderPath, folderName, function(count) {
		setProgress(true, T.collectFiles + ': ' + count, Math.min(5, count));
	})
		.then(function(fileList) {
			if (downloadCancelled) {
				return Promise.reject(new Error('Cancelled'));
			}
			if (!fileList.length) {
				endDownloadJob();
				setProgress(false);
				log(T.emptyDownload, 'error');
				return;
			}
			runDownloadZip(fileList, zipName, 95);
		})
		.catch(handleDownloadZipError);
}

function bindUploadControls() {
	$('btnUploadFiles').onclick = function() {
		$('fileToUp').click();
	};
	$('btnUploadFolder').onclick = function() {
		$('folderToUp').click();
	};
	$('fileToUp').onchange = function() {
		if (!this.files || !this.files.length) {
			return;
		}
		queueUploads(this.files, false);
		this.value = '';
	};
	$('folderToUp').onchange = function() {
		if (!this.files || !this.files.length) {
			return;
		}
		queueUploads(this.files, true);
		this.value = '';
	};
}

function bindToolbar() {
	$('btnMkdir').onclick = mkdir;
	$('btnRefresh').onclick = function() {
		rddir(curDir);
	};
	$('btnMuteGs').onclick = muteGs;
	$('btnMuteAy').onclick = muteAy;
	$('dirName').addEventListener('keydown', function(e) {
		if (e.key === 'Enter') {
			mkdir();
		}
	});
}

function bindDropzone() {
	if (isMobileUi()) {
		return;
	}
	var dropzone = $('dropzone');
	var dragDepth = 0;
	function onDragEnter(e) {
		e.preventDefault();
		e.stopPropagation();
		dragDepth++;
		dropzone.classList.add('dragover');
		if (e.dataTransfer) {
			e.dataTransfer.dropEffect = 'copy';
		}
	}
	function onDragOver(e) {
		e.preventDefault();
		e.stopPropagation();
		if (e.dataTransfer) {
			e.dataTransfer.dropEffect = 'copy';
		}
		dropzone.classList.add('dragover');
	}
	function onDragLeave(e) {
		e.preventDefault();
		e.stopPropagation();
		dragDepth--;
		if (dragDepth <= 0) {
			dragDepth = 0;
			dropzone.classList.remove('dragover');
		}
	}
	function onDrop(e) {
		e.preventDefault();
		e.stopPropagation();
		dragDepth = 0;
		dropzone.classList.remove('dragover');
		var dataTransfer = e.dataTransfer;
		if (!dataTransfer) {
			return;
		}
		var entrySnapshots = snapshotDropEntries(dataTransfer);
		if (!entrySnapshots.length && (!dataTransfer.files || !dataTransfer.files.length)) {
			beginCommand('');
			log(T.noUploadFiles, 'error');
			return;
		}
		beginUploadJob();
		beginCommand(T.uploadProgress + '...');
		setProgress(true, T.collectFiles + '...', 0);
		collectAllDropUploadTasks(dataTransfer)
			.then(function(tasks) {
				if (uploadCancelled) {
					return;
				}
				if (!tasks.length) {
					endUploadJob();
					setProgress(false);
					beginCommand('');
					log(T.noUploadFiles, 'error');
					return;
				}
				queueUploadTasks(tasks);
			})
			.catch(function(err) {
				if (uploadCancelled) {
					return;
				}
				endUploadJob();
				setProgress(false);
				beginCommand('');
				log(T.uploadError + ': ' + escapeHtml(err.message), 'error');
			});
	}
	dropzone.addEventListener('dragenter', onDragEnter);
	dropzone.addEventListener('dragover', onDragOver);
	dropzone.addEventListener('dragleave', onDragLeave);
	dropzone.addEventListener('drop', onDrop);
}

initUI();
bindLangSelect();
bindMobileUi();
bindUploadControls();
bindToolbar();
bindDropzone();
bindSelectionBar();
$('btnCancelProgress').onclick = cancelProgressJob;
setLang(resolveUiLang(), false);
rddir('');
