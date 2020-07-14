let current_playlist_path = "";

function concat_path(path, name)
{
	return path ? path + '/' + name : name;
}

function delete_file(remote_path) {
	console.log("delete file " + remote_path);
	$.ajax({
		url: "api/v1/file/" + remote_path,
		type: 'DELETE',
		async: false
	});
}

function delete_dir(remote_path) {
	console.log("delete dir " + remote_path);
	$.ajax({
		url: "api/v1/dir/" + remote_path,
		type: 'DELETE',
		async: false
	});
}

function recursive_delete(remote_path) {
	$.ajax({
		url: "api/v1/dir/" + remote_path,
		type: 'GET',
		async: false,
		dataType: "json",
		success : function(response) {
			for (let i = 0; i < response.length; i++) {
				if (response[i]['type'] == "dir")
					recursive_delete(remote_path + '/' + response[i].name)
				else
					delete_file(remote_path + '/' + response[i].name)
			}
		}
	});
	delete_dir(remote_path);
}

function create_dir(remote_path) {
	console.log("create dir " + remote_path);
	$.ajax({
		url: "api/v1/dir/" + remote_path,
		type: 'POST',
		async: false
	});
}

function create_file(remote_path) {
	$.ajax({
		url: "api/v1/file/" + remote_path,
		type: 'POST',
		async: false,
	});
}

function getAsyncUrl(treeId, treeNode) {
	if (treeNode)
		return "api/v1/dir/" + treeNode['path']
	else
		return "api/v1/dir";
}

function musicFilter(treeId, parentNode, responseData) {
	var res = []

	for (let i = 0; i < responseData.length; i++) {
		var path = !parentNode ? responseData[i].name : parentNode.path + '/' + responseData[i].name;
		var isParent = responseData[i]['type'] == "dir" ? true : false;

		/* Start from music directory but remove others from root */
		if (!parentNode && responseData[i].name != "music.db")
			continue;

		res.push({'name': responseData[i].name, 'path': path, isParent: isParent});
	}

	return res;
}

function BeforeDragToPlaylist(treeId, treeNodes)
{
	console.log(treeNodes);

	/* for the moment only allow files dnd */
	return !treeNodes[0].isParent;

	/* FIXME : later on remove above restriction and use below code */
	/* if multiple drop nodes then can't be root one */
	if (treeNodes.length > 1)
		return true;

	return treeNodes[0].level >= 1;
}

function buildPlaylistElem(name)
{
	let new_elem = $("<li class='playlist_elem'>" + name + "<span class='button remove foobar'></span></li>");

	new_elem.children('span').bind('click', remove_elem);

	return new_elem;
}

function OnDropToPlaylist(e, treeId, treeNodes, targetNode, moveType, isCopy)
{
	let container = $("#playlist");

	console.log(container.hasClass("active"));
	if (!container.hasClass("active"))
		return ;

	if (e.target.id == "playlist" || $(e.target).parents("#playlist").length > 0) {
		let is_append = e.target.id == "playlist";
		let current = $(e.target);

		for (let i = 0; i < treeNodes.length; i++) {
			let paths = treeNodes[i].path.split('/');
			let path = '';

			for (let i = 1; i < paths.length; i++)
				path = concat_path(path, paths[i]);

			if (is_append) {
				container.append(buildPlaylistElem(path));
			} else {
				let elem = buildPlaylistElem(path);

				current.after(elem);
				current = elem;
			}
		}
	}
}

var musicpanesetting = {
	async: {
		enable: true,
		type: "get",
		url: getAsyncUrl,
		dataFilter: musicFilter,
	},
	edit: {
		enable: true,
		showRemoveBtn: false,
		showRenameBtn: false,
		drag: {
			isMove: false
		}
	},
	callback: {
		beforeDrag: BeforeDragToPlaylist,
		onDrop: OnDropToPlaylist,
	}
};

function remove_elem(event) {
	$(this).closest('li').remove();
}

function save_playlist(event) {
	let text = "";

	$('li', '#playlist').each(function(n) {
		text += $(this).text() + "\n";
	});
	console.log(text);

	$.ajax({
		url: "api/v1/file/" + current_playlist_path,
		type: 'POST',
		async: false,
		data: text.trim(),
	});

	cancel_playlist(event);
}

function cancel_playlist(event) {
	$('li', '#playlist').remove();
	$("#playlist").removeClass("active");
	$("#save").unbind('click');
	$("#cancel").unbind('click');
	$('button').attr("disabled", "disabled");
}

function display_playlist(response) {
	console.log(response);

	if (response.trim() != "") {
		let playlist_elts = response.split("\n");
		let container = $("#playlist");

		for (let i = 0; i < playlist_elts.length; i++)
			container.append(buildPlaylistElem(playlist_elts[i]));
	}

	$("#playlist").addClass("active");
	$("#save").bind('click', save_playlist);
	$("#cancel").bind('click', cancel_playlist);
	$('button').removeAttr("disabled");
}

function playlistFilter(treeId, parentNode, responseData) {
	var res = []

	for (let i = 0; i < responseData.length; i++) {
		var path = !parentNode ? responseData[i].name : parentNode.path + '/' + responseData[i].name;
		var isParent = responseData[i]['type'] == "dir" ? true : false;

		/* Start from music directory but remove others from root */
		if (!parentNode && responseData[i].name != "playlist")
			continue;

		res.push({'name': responseData[i].name, 'path': path, isParent: isParent});
	}

	return res;
}

function hideRMenu() {
	let rMenu = $("#rMenu");

	rMenu.css({"visibility": "hidden"});
	$("body").unbind("mousedown", onBodyMouseDown);
}

function add_dir_menu() {
let zTree = $.fn.zTree.getZTreeObj("playlistpane");
	let node = zTree.getSelectedNodes()[0];

	hideRMenu();
	if (node) {
		zTree.addNodes(node, {pId:node.id, name: "new_folder", isParent: true, path: node.path + '/new_folder'});
		create_dir(node.path + '/new_folder');
	}
}

function del_dir_menu() {
	let zTree = $.fn.zTree.getZTreeObj("playlistpane");
	let node = zTree.getSelectedNodes()[0];

	hideRMenu();
	if (node)
		zTree.removeNode(node, true);
}

function rename_dir_menu() {
	let zTree = $.fn.zTree.getZTreeObj("playlistpane");
	let node = zTree.getSelectedNodes()[0];

	hideRMenu();
	if (node)
		zTree.editName(node);
}

function add_playlist_menu() {
	let zTree = $.fn.zTree.getZTreeObj("playlistpane");
	let node = zTree.getSelectedNodes()[0];

	hideRMenu();
	if (node) {
		zTree.addNodes(node, {pId:node.id, name: "new_playlist", isParent: false, path: node.path + '/new_playlist'});
		create_file(node.path + '/new_playlist');
	}
}

function del_playlist_menu() {
	let zTree = $.fn.zTree.getZTreeObj("playlistpane");
	let node = zTree.getSelectedNodes()[0];

	hideRMenu();
	if (node)
		zTree.removeNode(node, true);
}

function edit_playlist_menu() {
	let zTree = $.fn.zTree.getZTreeObj("playlistpane");
	let node = zTree.getSelectedNodes()[0];
	let container = $("#playlist");

	if (container.hasClass("active")) {
		hideRMenu();
		return ;
	}

	console.log("edit_playlist " + node.path);

	$.ajax({
		url: "api/v1/file/" + node.path,
		type: 'GET',
		async: false,
		success: display_playlist,
	});
	current_playlist_path = node.path;

	hideRMenu();
}

function rename_playlist_menu() {
	let zTree = $.fn.zTree.getZTreeObj("playlistpane");
	let node = zTree.getSelectedNodes()[0];

	hideRMenu();
	if (node)
		zTree.editName(node);
}

function BeforeRightClick(treeId, treeNode) {
	console.log("BeforeRightClick");
	return true;
}

function OnRightClick(event, treeId, treeNode) {
	let zTree = $.fn.zTree.getZTreeObj("playlistpane");

	if (treeNode) {
		zTree.selectNode(treeNode);
		showRMenu(treeNode, event.clientX, event.clientY);
	}
}

function showRMenu(treeNode, x, y) {
	let rMenu = $("#rMenu");

	console.log(treeNode);
	console.log(treeNode.isParent);

	if (treeNode.isParent) {
		$("#m_add_dir").show();
		$("#m_del_dir").show();
		$("#m_rename_dir").show();
		$("#m_add_playlist").show();
		$("#m_del_playlist").hide();
		$("#m_edit_playlist").hide();
		$("#m_rename_playlist").hide();
		if (treeNode.level == 0) {
			$("#m_del_dir").hide();
			$("#m_rename_dir").hide();
		}
	} else {
		$("#m_add_playlist").hide();
		$("#m_del_playlist").show();
		$("#m_edit_playlist").show();
		$("#m_rename_playlist").show();
		$("#m_add_dir").hide();
		$("#m_del_dir").hide();
		$("#m_rename_dir").hide();
	}

	y += document.body.scrollTop;
	x += document.body.scrollLeft;
	rMenu.css({"top":y+"px", "left":x+"px", "visibility":"visible"});

	$("body").bind("mousedown", onBodyMouseDown);
}

function onBodyMouseDown(event){
	let rMenu = $("#rMenu");

	if (!(event.target.id == "rMenu" || $(event.target).parents("#rMenu").length>0)) {
		rMenu.css({"visibility" : "hidden"});
	}
}

function onRenameDir(event, treeId, treeNode, isCancel) {
	let paths = treeNode.path.split('/');
	let path = '';

	$.ajax({
		url: "api/v1/dir/" + treeNode['path'],
		type: 'PUT',
		data: { new_name: treeNode.name },
		async: false
	});

	for (let i = 0; i < paths.length - 1; i++)
		path = concat_path(path, paths[i]);
	path = concat_path(path, treeNode.name);
	treeNode.path = path;
}

function onRenamePlaylist(event, treeId, treeNode, isCancel) {
	let paths = treeNode.path.split('/');
	let path = '';

	$.ajax({
		url: "api/v1/file/" + treeNode['path'],
		type: 'PUT',
		data: { new_name: treeNode.name },
		async: false
	});

	for (let i = 0; i < paths.length - 1; i++)
		path = concat_path(path, paths[i]);
	path = concat_path(path, treeNode.name);
	treeNode.path = path;
	/* FIXME : need to update all sub nodes path ..... */
}

function onRenameNode(event, treeId, treeNode, isCancel) {
	if (treeNode.isParent)
		onRenameDir(event, treeId, treeNode, isCancel);
	else
		onRenamePlaylist(event, treeId, treeNode, isCancel);
}

function onRemoveNode(event, treeId, treeNode) {
	if (treeNode.isParent) {
		recursive_delete(treeNode['path']);
	} else
		delete_file(treeNode['path']);
}

var playlistpanesetting = {
	async: {
		enable: true,
		type: "get",
		url: getAsyncUrl,
		dataFilter: playlistFilter,
	},
	callback: {
		beforeRightClick: BeforeRightClick,
		onRightClick: OnRightClick,
		onRename: onRenameNode,
		onRemove: onRemoveNode,
	}
};

$(document).ready(function() {
	$.fn.zTree.init($("#musicpane"), musicpanesetting, []);
	$.fn.zTree.init($("#playlistpane"), playlistpanesetting, []);
	$( "#playlist" ).sortable();
});

/*function add_new_line(response) {
	console.log(response);
	response = response + "\nhello";
	console.log(response);
	$.ajax({
		url: "api/v1/file/playlist/test.m3u",
		type: 'PUT',
		async: false,
		data: response,
	});
}

$(document).ready(function() {
	console.log("will read test.m3u");
	$.ajax({
		url: "api/v1/file/playlist/test.m3u",
		type: 'GET',
		async: false,
		success: add_new_line,
	});
	console.log("got it");

	console.log("create foo");
	$.ajax({
		url: "api/v1/file/playlist/foo",
		type: 'POST',
		async: false,
	});
	console.log("create foo done");
});*/