function getAsyncUrl(treeId, treeNode) {
	if (treeNode)
		return "api/v1/dir/" + treeNode['path']
	else
		return "api/v1/dir/";
}

function filter(treeId, parentNode, responseData) {
	var res = []

	for (let i = 0; i < responseData.length; i++) {
		var path = !parentNode ? responseData[i].name : parentNode.path + '/' + responseData[i].name;
		var isParent = responseData[i]['type'] == "dir" ? true : false;

		/* Start from music directory but remove others from root */
		if (!parentNode && responseData[i].name != "music" && responseData[i].name != "Music")
			continue;

		res.push({'name': responseData[i].name, 'path': path, isParent: isParent});
	}

	return res;
}

function delete_file(remote_path) {
	console.log("delete file " + remote_path);
	$.ajax({
		url: "api/v1/file/" + remote_path,
		type: 'DELETE',
		async: false
	});
}

function create_dir(remote_path) {
	console.log("create dir " + remote_path);
	$.ajax({
		url: "api/v1/dir/" + remote_path,
		type: 'POST',
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

function onRemoveNode(event, treeId, treeNode) {
	if (treeNode.isParent) {
		recursive_delete(treeNode['path']);
	} else
		delete_file(treeNode['path']);
}

function onRenameNode(e, treeId, treeNode, isCancel) {
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

function showRemoveBtn(treeId, treeNode) {
	/* avoid to delete root dir */
	return treeNode.level;
}

function showRenameBtn(treeId, treeNode) {
	if (!treeNode.level)
		return false;

	return treeNode.isParent;
}

var newCount = 1;
function addHoverDom(treeId, treeNode) {
	if (!treeNode.isParent)
		return ;
	if (treeNode.editNameFlag || $("#addBtn_"+treeNode.tId).length>0)
		return;
	var sObj = $("#" + treeNode.tId + "_span");
	var addStr = "<span class='button add' id='addBtn_" + treeNode.tId
		+ "' title='add node' onfocus='this.blur();'></span>";
	sObj.after(addStr);
	var btn = $("#addBtn_"+treeNode.tId);
	if (btn) btn.bind("click", function() {
		var zTree = $.fn.zTree.getZTreeObj("treeDemo");
		zTree.addNodes(treeNode, {id:(10000 + newCount), pId:treeNode.id, name: "new_dir" + newCount, isParent: true, path: treeNode.path + '/new_dir' + newCount});
		create_dir(treeNode.path + '/new_dir' + newCount);
		newCount++;
		return false;
	});
};

function removeHoverDom(treeId, treeNode) {
	$("#addBtn_"+treeNode.tId).unbind().remove();
};

var setting = {
	async: {
		enable: true,
		type: "get",
		url: getAsyncUrl,
		dataFilter: filter,
	},
	view: {
		addHoverDom: addHoverDom,
		removeHoverDom: removeHoverDom,
		selectedMulti: false,
	},
	callback: {
		onRemove: onRemoveNode,
		onRename: onRenameNode
	},
	edit: {
		enable:true,
		showRenameBtn: showRenameBtn,
		showRemoveBtn: showRemoveBtn,
		removeTitle: "remove",
		drag: {
			isMove: false,
			isCopy: false
		}
	}
};

function create_dir(remote_path) {
	console.log("create dir " + remote_path);
	$.ajax({
		url: "api/v1/dir/" + remote_path,
		type: 'POST',
		async: false
	});
}

function concat_path(path, name)
{
	return path ? path + '/' + name : name;
}

function create_remote_dirs(remote_path)
{
	let paths = remote_path.split('/');
	let path = '';

	for (let i = 0; i < paths.length - 1; i++) {
		path = concat_path(path, paths[i]);

		create_dir(path);
	}
}

function upload_file_for_real(path, file)
{
	let data;
	let request;

	path = "/" + path;
	console.log("upload " + path);
	data = new FormData();
	data.append("content", file, path);
	request = new XMLHttpRequest();
	request.open("POST", 'api/v1/upload', false);
	$("#status").text("Upload " + path);
	request.send(data);
}

/*function upload_file_for_real(path, file)
{
	let reader = new FileReader();

	console.log("upload " + path);

	if (true) {
		let data = new FormData();
		file.webkitRelativePath = path;
		console.log(file);
		console.log(path);
		data.append("content", file, path);

		if (true) {
			var request = new XMLHttpRequest();
			request.open("POST", 'api/v1/upload', false);
			request.send(data);
		} else {
			$.ajax({
				url: 'api/v1/upload',
				type: 'POST',
				data: data,
				processData: false,
				contentType: false,
				cache: false,
				//contentType: 'multipart/form-data',
				async: false
			});
		}
	} else {
	reader.onload = function(e) {
			console.log("start upload");
			let formData = new FormData();
			formData.append("filename", file);
			$.ajax({
				url: 'api/v1/upload/' + path,
				type: 'POST',
				data: formData,
				processData: false,
				contentType: 'multipart/form-data',
				async: false
			});
			console.log("upload done");
		}
		console.log(file);
		reader.readAsBinaryString(file);
	}
}*/

let save_files;
function upload_file(node, path)
{
	let remote_path = concat_path(path, node.name);

	create_remote_dirs(remote_path);
	//console.log(node);

	//node.file loose its type ... ise global until I find a solution
	//upload_file_for_real(remote_path, node.file);
	upload_file_for_real(remote_path, save_files[node.idx]);
}

function recursiveFileParse(nodes, path)
{
	for (let i=0; i<nodes.length; i++) {
		let node = nodes[i];

		if ("file" in node)
			upload_file(node, path);
		else
			recursiveFileParse(node.children, concat_path(path, node.name));
	}
}

function myOnDrop(event, treeId, treeNodes, targetNode, moveType)
{
	let path = targetNode ? targetNode.path : '';
	recursiveFileParse(treeNodes, path);
}

var setting2 = {
	callback: {
		onDrop: myOnDrop
	},
	edit: {
		enable: true,
		showRemoveBtn: false,
		showRenameBtn: false,
		drag: {
			isMove: false
		}
	}
};

function get_or_create(nodes, entry_name) {
	for (let i=0; i<nodes.length; i++) {
		if (nodes[i].name == entry_name) {
			return nodes[i]['children'];
		}
	}

	var res = {name: entry_name, children: []};
	nodes.push(res);

	return res['children'];
}

$(document).ready(function(){
	$.fn.zTree.init($("#treeDemo"), setting, []);
	$("#filepicker").bind('change', function (event) {
		//let files = event.target.files;
		let files = document.getElementById("filepicker").files;
		var nodes = [];
		var current;

		/* use global until I fix pb */
		save_files = files;

		for (let i=0; i<files.length; i++) {
			var j;
			var paths = files[i].webkitRelativePath.split('/');
			current = nodes;
			for (j=0; j<paths.length - 1; j++) {
				current = get_or_create(current, paths[j]);
			}
			//console.log(files[i]);
			current.push({name: paths[j], file: files[i], idx: i})
		}
		$.fn.zTree.init($("#treeDemo2"), setting2, nodes);
	});
});
