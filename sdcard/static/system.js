$(document).ready(function() {
	$.ajax({
		url: "api/v1/system",
		type: 'GET',
		async: false,
		dataType: "json",
		success : function(response) {
			console.log("success");
			console.log(response);
			$('#name').val(response['name']);
		},
		error : function() {
			console.log("error");
		}
	});
	$("#button_name").click(function() {
		$.ajax({
			url: "api/v1/system",
			type: 'POST',
			async: false,
			data: $('#form_name').serialize()
		});
	});
});