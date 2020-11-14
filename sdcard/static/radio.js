nodes = [];

function radiolist_to_nodes(radiolist)
{
	var res = [];

	for (let i = 0; i < radiolist.length; i++) {
		var is_radio = radiolist[i]['radio'] ? true : false;

		if (is_radio) {
			res.push({'name': radiolist[i]['radio'],
					  'url': radiolist[i]['url'],
					  'path': radiolist[i]['path'],
					  'port': radiolist[i]['port'],
					  'rate': radiolist[i]['rate']});
		} else {
			res.push({'name': radiolist[i]['folder'], children: radiolist_to_nodes(radiolist[i]['entries'])});
		}
	}

	return res;
}

function nodes_to_radiolist(nods)
{
	var res = [];

	for (let i = 0; i < nods.length; i++) {
		var is_radio = nods[i]['children'] ? false : true;

		if (is_radio) {
			res.push({'radio': nods[i]['name'],
					  'url': nods[i]['url'],
					  'path': nods[i]['path'],
					  'port': nods[i]['port'],
					  'rate': nods[i]['rate']});
		} else {
			res.push({'folder': nods[i]['name'], 'entries': nodes_to_radiolist(nods[i]['children'])});
		}
	}

	return res;
}

function get_radio_db()
{
	console.log("ask for radio db");
	$.ajax({
		url: "api/v1/file/radio/radio.db",
		type: 'GET',
		async: false,
		dataType: "json",
		success : function(response) {
			console.log("success");
			console.log(response);
			nodes.push({'name': 'radio', children: radiolist_to_nodes(response), open: true})
		},
		error : function() {
			console.log("error");
		}
	});
}

function save_radio(event)
{
	let radio = event.data;
	let datas = $('form', '#radio').serializeArray();

	for (let i = 0; i < datas.length; i++) {
		radio[datas[i]['name']] = datas[i]['value'];
	}
	cancel_radio(event);
}

function cancel_radio(event)
{
	$('form', '#radio').remove();
	$("#radio").removeClass("active");
	$("#save").unbind('click');
	$("#cancel").unbind('click');
	$('button').attr("disabled", "disabled");
}

function edit_node(radio)
{
	let container = $("#radio");

	container.append($("\
<form class=\"form-radio\"> \
	<div class=\"form-radio\">\
		<label for=\"url\">url: </label> \
		<input type=\"text\" name=\"url\" value=\"" + radio['url'] + "\"> \
	</div>\
	<div class=\"form-radio\">\
		<label for=\"path\">path: </label> \
		<input type=\"text\" name=\"path\" value=\"" + radio['path'] + "\"> \
	</div>\
	<div class=\"form-radio\">\
		<label for=\"port\">port: </label> \
		<input type=\"text\" name=\"port\" value=\"" + radio['port'] + "\"> \
	</div>\
	<div class=\"form-radio\">\
		<label for=\"rate\">rate: </label> \
		<input type=\"text\" name=\"rate\" value=\"" + radio['rate'] + "\"> \
	</div>\
</form>\
	"));

	$("#radio").addClass("active");
	$("#save").bind('click', radio, save_radio);
	$("#cancel").bind('click', radio, cancel_radio);
	$('button').removeAttr("disabled");
}

function hideRMenu()
{
	$("div.menu:visible").hide();
	$("body").unbind("mousedown", onBodyMouseDown);
}

function add_folder()
{
	let zTree = $.fn.zTree.getZTreeObj("radiolistpane");
	let node = zTree.getSelectedNodes()[0];

	hideRMenu();
	if (node)
		zTree.addNodes(node, {'name': 'new_folder', children: []});
}

function del_node()
{
	let zTree = $.fn.zTree.getZTreeObj("radiolistpane");
	let node = zTree.getSelectedNodes()[0];

	hideRMenu();
	if (node)
		zTree.removeNode(node, true);
}

function rename_node()
{
	let zTree = $.fn.zTree.getZTreeObj("radiolistpane");
	let node = zTree.getSelectedNodes()[0];

	hideRMenu();
	if (node)
		zTree.editName(node);
}

function add_radio()
{
	let zTree = $.fn.zTree.getZTreeObj("radiolistpane");
	let node = zTree.getSelectedNodes()[0];

	hideRMenu();
	if (node)
		zTree.addNodes(node, {'name': 'new_radio',
							  'url': 'invalid',
							  'path': '/',
							  'port': '80',
							  'rate': '44100'});
}

function edit_radio()
{
	let zTree = $.fn.zTree.getZTreeObj("radiolistpane");
	let node = zTree.getSelectedNodes()[0];

	if ($("#radio").hasClass("active")) {
		hideRMenu();
		return ;
	}

	hideRMenu();
	edit_node(node);
}

function save()
{
	let zTree = $.fn.zTree.getZTreeObj("radiolistpane");
	let nods = zTree.getNodes();
	let radiolist = nodes_to_radiolist(nods[0]['children']);

	console.log(radiolist);
	$.ajax({
		url: "api/v1/file/radio/radio.db",
		type: 'POST',
		async: false,
		data: JSON.stringify(radiolist, null, ' '),
	});
	hideRMenu();
}

function onBodyMouseDown(event){
	let rMenu = $("div.menu:visible");

	if (!(event.target.id == rMenu.attr('id') || $(event.target).parents("div.menu:visible").length>0)) {
		hideRMenu();
	}
}

function showMenu(rMenu, x, y)
{
	y += document.body.scrollTop;
	x += document.body.scrollLeft;
	rMenu.css({"top":y+"px", "left":x+"px"}).show();
	$("body").bind("mousedown", onBodyMouseDown);
}

function showRMenu(treeNode, x, y)
{
	if (treeNode.isParent) {
		if (treeNode.level == 0)
			showMenu($("#rMenuTop"), x, y);
		else
			showMenu($("#RMenuFolder"), x, y);
	} else
		showMenu($("#RMenuRadio"), x, y);
}

function BeforeRightClick(treeId, treeNode)
{
	return true;
}

function OnBeforeDrop(treeId, treeNodes, targetNode, moveType, isCopy)
{
	console.log(targetNode);
	console.log(moveType);

	if (!targetNode)
		return false;

	if (moveType == "inner" && !targetNode.isParent)
		return false;

	return true;
}

function OnRightClick(event, treeId, treeNode)
{
	let zTree = $.fn.zTree.getZTreeObj("radiolistpane");

	if (treeNode) {
		zTree.selectNode(treeNode);
		showRMenu(treeNode, event.clientX, event.clientY);
	}
}

var radiolistpanesetting = {
	data: {
		keep: {
			parent: true
		}
	},
	edit: {
		enable: true,
		showRemoveBtn: false,
		showRenameBtn: false,
		drag: {
			isCopy: false
		}
	},
	callback: {
		beforeRightClick: BeforeRightClick,
		onRightClick: OnRightClick,
		beforeDrop: OnBeforeDrop
	}
};

$(document).ready(function() {
	$("div.menu").hide();
	get_radio_db();
	$.fn.zTree.init($("#radiolistpane"), radiolistpanesetting, nodes);
});
