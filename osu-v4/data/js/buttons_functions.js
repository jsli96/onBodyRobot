/**
 * @license
 * Copyright 2020 Sébastien CANET
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @fileoverview Helper functions for buttons visible in UI.
 * @author scanet@libreduc.cc (SebCanet)
 */

// Ensure the flag is defined globally.
window.Code = window.Code || {};
if (typeof Code.imageConversionDone === 'undefined') {
  Code.imageConversionDone = false;
}

/*
 * auto save and restore blocks
 */
function auto_save_and_restore_blocks() {
    // Store the blocks for the duration of the reload.
    // MSIE 11 does not support sessionStorage on file:// URLs.
    if (window.sessionStorage) {
        var xml = Blockly.Xml.workspaceToDom(Blockly.getMainWorkspace());
        var text = Blockly.Xml.domToText(xml);
        window.sessionStorage.loadOnceBlocks = text;
    }
}
;

var fullScreen_ = false;

/**
 * Full screen, thanks to HTML5 API
 * @argument {type} _element 
 */
function fullScreen(_element) {
    var elementClicked = _element || document.documentElement;
    // HTML5
    if (document.fullscreenEnabled) {
        if (!document.fullscreenElement) {
            elementClicked.requestFullscreen();
            document.addEventListener('fullscreenchange', exitFullScreen, false);
        } else {
            exitFullScreen();
            document.exitFullscreen();
            document.removeEventListener('fullscreenchange', exitFullScreen, false);
        }
    } else
    // Chrome, Safari and Opera
    if (document.webkitFullscreenEnabled) {
        if (!document.webkitFullscreenElement) {
            elementClicked.webkitRequestFullscreen();
            document.addEventListener('webkitfullscreenchange', exitFullScreen, false);
        } else {
            exitFullScreen();
            document.webkitExitFullscreen();
            document.removeEventListener('webkitfullscreenchange', exitFullScreen, false);
        }
    } else
    // IE/Edge
    if (document.msFullscreenEnabled) {
        if (!document.msFullscreenElement) {
            elementClicked.msRequestFullscreen();
            document.addEventListener('MSFullscreenChange', exitFullScreen, false);
        } else {
            exitFullScreen();
            document.msExitFullscreen();
            document.removeEventListener('MSFullscreenChange', exitFullScreen, false);
        }
    }
}
;

function exitFullScreen() {
  // Check if no element is in full screen mode (standard + vendor prefixes)
  if (!document.fullscreenElement && 
      !document.webkitFullscreenElement && 
      !document.msFullscreenElement) {
    // Full screen has been exited:
    fullScreenButton.className = 'iconButtons';
    fullScreen_ = false;
    // Reload the page
    location.reload();
  } else {
    // If still in full screen, update button state (if needed)
    fullScreenButton.className = 'iconButtonsClicked';
    fullScreen_ = true;
  }
};


/**
 * Copy code from div code_peek in clipboard system
 */
Code.copyToClipboard = function () {
    if (document.selection) { // IE
        var range = document.body.createTextRange();
        range.moveToElementText(document.getElementsByClassName("ace_content")[0]);
        range.select();
        document.execCommand("copy");
    } else if (window.getSelection) {
        // var range = document.createRange();
        // range.selectNode(document.getElementsByClassName("ace_content")[0]);
        // window.getSelection().removeAllRanges();
        // window.getSelection().addRange(range);
    // }
    // document.execCommand("copy");
        navigator.clipboard.writeText(document.getElementsByClassName("ace_content")[0].innerText)
                .then(() => {console.log('Code copied!') })
                .catch((error) => { console.log('Copy failed! ${error}') });
    }
};

/**
 * modal controllers
 */
Code.boardsListModalShow = function () {
    document.getElementById('overlayForModals').style.display = "block";
    document.getElementById('boardListModal').classList.add('show');
    for (var i = 0; i < document.getElementById("boardDescriptionSelector").length; i++)
        document.getElementById("boardDescriptionSelector").options[i].style.backgroundColor = 'white';
    var boardValue = document.getElementById("boardMenu").value;
    if (boardValue !== 'none') {
        document.getElementById("boardDescriptionSelector").selectedIndex = boardValue;
        document.getElementById("boardDescriptionSelector").value = boardValue;
        document.getElementById("boardDescriptionSelector").options[document.getElementById("boardDescriptionSelector").selectedIndex].style.backgroundColor = 'yellow';
    }
    window.addEventListener('click', Code.boardsListModalHide, 'once');
    Code.boardDescription();
};
Code.portsListModalShow = function () {
    document.getElementById('overlayForModals').style.display = "block";
    document.getElementById('portListModal').classList.add('show');
    var portValue = document.getElementById("serialMenu").value;
    if (portValue !== 'none') {
        document.getElementById("serialMenu").selectedIndex = portValue;
        document.getElementById("serialMenu").value = portValue;
    }
    window.addEventListener('click', Code.portsListModalHide, 'once');
};
document.getElementById("closeModalBoards").onclick = function () {
    document.getElementById('overlayForModals').style.display = "none";
    document.getElementById('boardListModal').classList.remove('show');
};
document.getElementById("closeModalPorts").onclick = function () {
    document.getElementById('overlayForModals').style.display = "none";
    document.getElementById('portListModal').classList.remove('show');
};
// When the user clicks anywhere outside of the modal, close it
Code.boardsListModalHide = function (event) {
    if (!document.getElementById('boardListModal').contains(event.target)) {
        document.getElementById('overlayForModals').style.display = "none";
        document.getElementById('boardListModal').classList.remove('show');
    }
};
Code.portsListModalHide = function (event) {
    if (!document.getElementById('portListModal').contains(event.target)) {
        document.getElementById('overlayForModals').style.display = "none";
        document.getElementById('portListModal').classList.remove('show');
    }
};

/**
 * change information in the boards modal
 **/
Code.boardDescription = function () {
    var boardValue = document.getElementById("boardDescriptionSelector").value;
    if (boardValue === '')
        boardValue = 'none';
    document.getElementById("board_mini_picture").setAttribute("src", profile[boardValue][0]['picture']);
    document.getElementById("board_connect").textContent = profile[boardValue][0]['usb'];
    document.getElementById("board_cpu").textContent = profile[boardValue][0]['cpu'];
    document.getElementById("board_voltage").textContent = profile[boardValue][0]['voltage'];
    document.getElementById("board_inout").textContent = profile[boardValue][0]['inout'];
};

/**
 * Undo/redo functions
 */
Code.Undo = function () {
    Blockly.getMainWorkspace().undo(0);
};
Code.Redo = function () {
    Blockly.getMainWorkspace().undo(1);
};

/**
 * Luanch blockFatcory with language argument
 */
Code.BlockFactory = function () {
    var lang = Code.getStringParamFromUrl('lang', '');
    if (!lang) {
        lang = "en";
    }
    parent.open('tools/blockFactory/blockFactory.html?lang=' + lang);
};

/**
 * Creates an INO file containing the Arduino code from the Blockly workspace and
 * prompts the users to save it into their local file system.
 */
Code.newProject = function () {
    var count = Code.workspace.getAllBlocks().length;
    if (count > 0) {
        Blockly.confirm(Blockly.Msg['DELETE_ALL_BLOCKS'].replace('%1', count), function (confirm) {
            if (confirm)
                Code.workspace.clear();
                return true;
        });
    }
};

/**
 * Creates an INO file containing the Arduino code from the Blockly workspace and
 * prompts the users to save it into their local file system.
 */
Code.saveCodeFile = function () {
    var utc = new Date().toJSON().slice(0, 10).replace(/-/g, '_');
    var dataToSave = Blockly.Arduino.workspaceToCode(Code.workspace);
    var blob = new Blob([dataToSave], {
        type: 'text/plain;charset=utf-8'
    });
    Blockly.prompt(MSG['save_span'], 'code', function (fileNameSave) {
        if (fileNameSave) {
            var fakeDownloadLink = document.createElement("a");
            fakeDownloadLink.download = fileNameSave + ".ino";
            fakeDownloadLink.href = window.URL.createObjectURL(blob);
            fakeDownloadLink.onclick = function destroyClickedElement(event) {
                document.body.removeChild(event.target);
            };
            fakeDownloadLink.style.display = "none";
            document.body.appendChild(fakeDownloadLink);
            fakeDownloadLink.click();
        }
    });
};



/**
  * Creats an INO file containing the Arduino code from the Blockly workspace
  * and posts it to http://127.0.0.1/verify/ which will pass it to the 
  * Arduino IDE with the --verify flag.
  */

Code.verifyCodeFile = function () {
    var code = Blockly.Arduino.workspaceToCode(Code.workspace);
    var boardId = Code.getStringParamFromUrl('board', '');
    
    alert("Ready to verify to Arduino.");
    
    Code.uploadCode(code, boardId, 'verify', 
                    function(status, response, errorInfo) {
                        var element = document.getElementById("content_serial");
                        element.innerHTML = response;
                        if (status == 200) {
                            alert("Program verified ok");
                        } else {
                            alert("Error verifying program: " + errorInfo);
                        }
                    });
};

/**
  * Creats an INO file containing the Arduino code from the Blockly workspace
  * and posts it to http://127.0.0.1/upload/ which will pass it to the 
  * Arduino IDE with the --verify flag.
  */

Code.uploadCodeFile = function () {
    var code = Blockly.Arduino.workspaceToCode(Code.workspace);
    var boardId = Code.getStringParamFromUrl('board', '');
    
    alert("Ready to upload to Arduino.");
    
    Code.uploadCode(code, boardId, 'upload', 
                    function(status, response, errorInfo) {
                        var element = document.getElementById("content_serial");
                        element.innerHTML = response;
                        if (status == 200) {
                            alert("Program uploaded ok");
                        } else {
                            alert("Error uploading program: " + errorInfo);
                        }
                    });
};

Code.uploadCode = function (code, boardId, mode, callback) {
    //var spinner = new Spinner().spin(target);

    var boardSpecs = {
        "arduino_leonardo": "arduino:avr:leonardo",
        "arduino_mega": "arduino:avr:mega",
        "arduino_micro": "arduino:avr:micro",
        "arduino_mini": "arduino:avr:mini",
        "arduino_nano": "arduino:avr:nano",
        "arduino_pro8": "arduino:avr:pro",
        "arduino_pro16": "arduino:avr:pro",
        "arduino_uno": "arduino:avr:uno",
        "arduino_yun": "arduino:avr:yun",
        "lilypad": "arduino:avr:lilypad"
    };
    var url = "http://127.0.0.1:8080/" + mode + "/";
    var method = "POST";
    var async = true;
    var request = new XMLHttpRequest();
    var comma = "";
    
    if (boardId != '') {
        url += "board=" + boardSpecs[boardId];
        comma = ","
    }
    
    if (document.getElementById("detailedCompilation").checked) {
        url += comma + "verbose=";
    }
    request.onreadystatechange = function() {
        if (request.readyState != 4) { 
            return; 
        }
        
        //spinner.stop();
        
        var status = parseInt(request.status); // HTTP response status, e.g., 200 for "200 OK"
        var errorInfo = null;
        var response = request.response;

        switch (status) {
        case 200:
            break;
        case 0:
            errorInfo = "code 0\n\nCould not connect to server at " + url + ".  Is the local web server running?";
            break;
        case 400:
            errorInfo = "code 400\n\nBuild failed - probably due to invalid source code.  Make sure that there are no missing connections in the blocks.";
            break;
        case 500:
            errorInfo = "code 500\n\nUpload failed.  Is the Arduino connected to USB port?";
            break;
        case 501:
            errorInfo = "code 501\n\nUpload failed.  Is 'ino' installed and in your path?  This only works on Mac OS X and Linux at this time.";
            break;
        default:
            errorInfo = "code " + status + "\n\nUnknown error.";
            break;
        };
        
        callback(status, response, errorInfo);
    };

    request.open(method, url, async);
    request.setRequestHeader("Content-Type", "text/plain;charset=UTF-8");
    request.send(code);	     
    
};

/**
 * Creates an XML file containing the blocks from the Blockly workspace and
 * prompts the users to save it into their local file system.
 */
Code.saveXmlBlocklyFile = function () {
    var xmlData = Blockly.Xml.workspaceToDom(Code.workspace);
    var dataToSave = Blockly.Xml.domToPrettyText(xmlData);
    var blob = new Blob([dataToSave], {
        type: 'text/xml;charset=utf-8'
    });
    Blockly.prompt(MSG['save_span'], 'blockly', function (fileNameSave) {
        if (fileNameSave) {
            var fakeDownloadLink = document.createElement("a");
            fakeDownloadLink.download = fileNameSave + ".bduino";
            fakeDownloadLink.href = window.URL.createObjectURL(blob);
            fakeDownloadLink.onclick = function destroyClickedElement(event) {
                document.body.removeChild(event.target);
            };
            fakeDownloadLink.style.display = "none";
            document.body.appendChild(fakeDownloadLink);
            fakeDownloadLink.click();
        }
    });
};

/**
 * Load an image from a local file (supports any image type).
 */
Code.loadImageFile = function () {
    // Define the event handler for when an image file is selected.
    var parseInputImageFile = function (e) {
        var files = e.target.files;
        if (!files || files.length === 0) {
            console.error('No file selected.');
            return;
        }
        var reader = new FileReader();

        // Once the file is loaded, create and display an image element.
        reader.onload = function (event) {
            var imageDataUrl = event.target.result; // Data URL of the image.
            // Create an <img> element.
            var imgElement = document.createElement('img');
            imgElement.src = imageDataUrl;
            imgElement.alt = 'Loaded Image';

            // Optionally, set width/height or add any additional styling here.
            // For example: imgElement.style.maxWidth = "100%";
            
            // Append the image to a container element if it exists.
            var container = document.getElementById('imageContainer');
            if (!container) {
                document.body.appendChild(imgElement);
            } else {
                container.appendChild(imgElement);
            }
        };

        // Handle file reading errors.
        reader.onerror = function (error) {
            console.error('Error reading image file:', error);
        };

        // Read the file as a Data URL.
        reader.readAsDataURL(files[0]);
    };

    // Create or retrieve the invisible file input element.
    var selectFile = document.getElementById('select_file');
    if (selectFile === null) {
        var selectFileDom = document.createElement('input');
        selectFileDom.type = 'file';
        selectFileDom.id = 'select_file';
        // Accept any image file type: PNG, JPEG, GIF, etc.
        selectFileDom.accept = 'image/*';
        selectFileDom.style.display = 'none';
        document.body.appendChild(selectFileDom);
        selectFile = document.getElementById('select_file');

        // Attach the change event listener.
        selectFile.addEventListener('change', parseInputImageFile, false);
    }

    // Optionally remove the input element once a file is clicked.
    selectFile.onclick = function destroyClickedElement(event) {
        document.body.removeChild(event.target);
    };

    // Trigger the file selection dialog.
    selectFile.click();
};

/**
 * Convert an uploaded image file into a C header file (.h) 
 * that contains a byte array representing the image data in hexadecimal format.
 * Once a picture is uploaded and converted, further clicks will not trigger the file explorer.
 */
// Code.uploadImageForConversion = function () {
//         const fileInput = document.createElement("input");
//         fileInput.type = "file";
//         fileInput.accept = "image/*";
//         fileInput.style.display = "none";
//         document.body.appendChild(fileInput);
      
//         fileInput.addEventListener("change", function(event) {
//           const file = event.target.files[0];
//           if (!file) {
//             console.error("No file selected.");
//             return;
//           }
          
//           const formData = new FormData();
//           formData.append("file", file);
      
//  // Send the file to the server's upload_and_convert endpoint.
//  fetch("http://127.0.0.1:5001/upload_and_convert", { 
//     method: "POST",
//     body: formData
//   })
//     .then(response => response.json())
//     .then(data => {
//       console.log("Server response:", data);
//       // Optionally, remove the file input element after successful upload.
//       document.body.removeChild(fileInput);
//     })
//     .catch(err => {
//       console.error("Error uploading file:", err);
//     });
// });
        
//         fileInput.click();
//       }
  
Code.uploadImageForConversion = function () {
    // Create an invisible file input element.
    const fileInput = document.createElement("input");
    fileInput.type = "file";
    fileInput.accept = "image/png";
    fileInput.style.display = "none";
    document.body.appendChild(fileInput);

    // Add an event listener to send the file when one is selected.
    fileInput.addEventListener("change", function(event) {
        const file = event.target.files[0];
        if (!file) {
            console.error("No file selected.");
            return;
        }
        
        // Prepare the form data.
        const formData = new FormData();
        formData.append("file", file);
        
        // Send the file to the server's new upload endpoint.
        // This endpoint should save the raw image for SPIFFS (e.g., "upload_image")
        fetch("http://127.0.0.1:5001/upload_image", { 
            method: "POST",
            body: formData
        })
        .then(response => response.json())
        .then(data => {
            console.log("Server response:", data);
            // Remove the file input element after a successful upload.
            document.body.removeChild(fileInput);
        })
        .catch(err => {
            console.error("Error uploading file:", err);
        });
    });
    
    // Trigger the file selection dialog.
    fileInput.click();
};



/**
 * Load blocks from local file.
 */
Code.loadXmlBlocklyFile = function () {
    // Create event listener function
    var parseInputXMLfile = function (e) {
        var files = e.target.files;
        var reader = new FileReader();
        reader.onloadend = function () {
            var success = false;
            if (reader.result != null) {
                Code.loadBlocksfromXml(reader.result);
                success = true;
            }
            if (success) {
                Code.workspace.render();
            } else {
                Blockly.alert(MSG['badXml'], callback);
            }
        };
        reader.readAsText(files[0]);
    };
    // Create once invisible browse button with event listener, and click it
    var selectFile = document.getElementById('select_file');
    if (selectFile === null) {
        var selectFileDom = document.createElement('INPUT');
        selectFileDom.type = 'file';
        selectFileDom.id = 'select_file';
        selectFileDom.accept = '.bduino, .xml';
        selectFileDom.style.display = 'none';
        document.body.appendChild(selectFileDom);
        selectFile = document.getElementById('select_file');
        selectFile.addEventListener('change', parseInputXMLfile, false);
    }

    selectFile.onclick = function destroyClickedElement(event) {
        document.body.removeChild(event.target);
    };

    selectFile.click();
};

/**
 * Convert an uploaded image file into a C header file (.h) 
 * that contains a byte array representing the image data in hexadecimal format.
 * Once a picture is uploaded and converted, further clicks will not trigger the file explorer.
 */
Code.convertImageToHeaderFile = function () {
    // Check if a conversion has already been done.
    if (Code.imageConversionDone) {
      console.log("Image already converted. No need to open the file explorer again.");
      return;
    }
  
    // Create an invisible file input element.
    const fileInput = document.createElement("input");
    fileInput.type = "file";
    fileInput.accept = "image/*"; // Accept any image type.
    fileInput.style.display = "none";
    document.body.appendChild(fileInput);
  
    // Add an event listener to process the file when it's selected.
    fileInput.addEventListener("change", function (event) {
      const file = event.target.files[0];
      if (!file) {
        console.error("No file selected.");
        return;
      }
  
      // Create a FileReader to read the file as an ArrayBuffer.
      const reader = new FileReader();
      reader.onload = function (e) {
        const arrayBuffer = e.target.result;
        const byteArray = new Uint8Array(arrayBuffer);
        const totalBytes = byteArray.length;
  
        // Build the header string.
        let headerContent = "";
        headerContent += `// array size is ${totalBytes}\n`;
        // Use the file name (without extension) as the array name.
        let arrayName = file.name.split(".")[0];
        headerContent += `static const unsigned char ${arrayName}[] PROGMEM = {\n`;
  
        // Convert each byte to a hexadecimal string.
        const bytesPerLine = 16;
        for (let i = 0; i < totalBytes; i++) {
          const hex = byteArray[i].toString(16).padStart(2, "0");
          headerContent += "0x" + hex;
          if (i !== totalBytes - 1) {
            headerContent += ", ";
          }
          // Insert a newline every few bytes for readability.
          if ((i + 1) % bytesPerLine === 0) {
            headerContent += "\n";
          }
        }
        headerContent += "\n};\n";
  
        // Create a Blob from the header content.
        const blob = new Blob([headerContent], { type: "text/plain" });
        const url = URL.createObjectURL(blob);
  
        // Log information about the generated Blob URL
        console.log(`Download URL generated: ${url}`);
        console.log(`The file should be downloaded to your browser’s default download folder.`);
  
        // Create a temporary <a> element to trigger a download.
        const downloadLink = document.createElement("a");
        downloadLink.href = url;
        // Append ".h" to the file base name.
        downloadLink.download = arrayName + ".h";
        document.body.appendChild(downloadLink);
        downloadLink.click();
  
        // Clean up: remove the temporary link and revoke the Blob URL.
        document.body.removeChild(downloadLink);
        URL.revokeObjectURL(url);
  
        // Set a flag so that the file explorer won’t pop up again.
        Code.imageConversionDone = true;
        
        // Remove the file input element now after processing.
        document.body.removeChild(fileInput);
      };
  
      reader.onerror = function (error) {
        console.error("Error reading file:", error);
      };
  
      // Read the file as an ArrayBuffer.
      reader.readAsArrayBuffer(file);
    });
  
    // Trigger the file selection dialog.
    fileInput.click();
  };
  

/**
 * Parses the XML from its input to generate and replace the blocks in the
 * Blockly workspace.
 * @param {!string} defaultXml String of XML code for the blocks.
 * @return {!boolean} Indicates if the XML into blocks parse was successful.
 */
Code.loadBlocksfromXml = function (defaultXml) {
    var count = Code.workspace.getAllBlocks().length;
    var xml = Blockly.Xml.textToDom(defaultXml);
    if (count > 0) {
        Blockly.confirm(MSG['loadXML_span'], function (confirm) {
            if (confirm)
                Code.workspace.clear();
                Blockly.Xml.domToWorkspace(xml, Code.workspace);
                return true;
        });
    } else {
        Blockly.Xml.domToWorkspace(xml, Code.workspace);
        return true;
    }
};

/**
 * Add or replace a parameter to the URL.
 *
 * @param {string} name The name of the parameter.
 * @param {string} value Value to set
 * @return {string} The url completed with parameter and value
 */
Code.addReplaceParamToUrl = function (url, param, value) {
    var re = new RegExp("([?&])" + param + "=.*?(&|$)", "i");
    var separator = url.indexOf('?') !== -1 ? "&" : "?";
    if (url.match(re)) {
        return url.replace(re, '$1' + param + "=" + value + '$2');
    } else {
        return url + separator + param + "=" + value;
    }
};

/**
 * Reset workspace and parameters
 */
Code.ResetWorkspace = function () {
    var count = Blockly.mainWorkspace.getAllBlocks(false).length;
    Blockly.confirm(MSG['resetQuestion_span'] + ' ' + Blockly.Msg['DELETE_ALL_BLOCKS'].replace('%1', count), function (answer) {
        if (answer) {
            Blockly.Events.disable();
            Blockly.getMainWorkspace().clear();
            Blockly.getMainWorkspace().trashcan.contents_ = [];
            Blockly.getMainWorkspace().trashcan.setLidOpen('false');
            window.removeEventListener('unload', auto_save_and_restore_blocks, false);
            localStorage.clear();
            sessionStorage.clear();
            Code.renderContent();
            Blockly.Events.enable();
            if (window.location.hash) {
                window.location.hash = '';
            }
            window.location = window.location.protocol + '//' + window.location.host + window.location.pathname;
        }
    });
};

/**
 * Change font size in blocks in all workspace
 */
Code.changeRenderingConstant = function (value) {
    var type = document.getElementById('rendering-constant-selector').value;
    switch (type) {
        case 'fontSizeBlocks':
            var fontStyle = {
                'size': value
            };
            Blockly.getMainWorkspace().getTheme().setFontStyle(fontStyle);
            editor.setOptions({
                fontSize: value + "pt"
            });
        case 'fontSizePage':
        // fontSizePageModify('access', value);
        case 'fontSpacingPage':
        // document.body.style.fontSize = value + 'px';
    }
    // Refresh theme.
    Blockly.getMainWorkspace().setTheme(Blockly.getMainWorkspace().getTheme());
};


/**
 * Hide/show the help modal.
 * @param {boolean} state The state of the checkbox. True if checked, false
 *     otherwise.
 */
var HelpModalDisplay_ = false;

function toggleDisplayHelpModal() {
    if (!HelpModalDisplay_) {
        document.getElementById('helpModal').style.display = 'block';
    	document.getElementById('helpModal').classList.add('show');
        document.getElementById('helpModal').style.left = (top.innerWidth - document.getElementById('helpModal').offsetWidth) / 2 + "px";
        document.getElementById('helpModal').style.top = (top.innerHeight - document.getElementById('helpModal').offsetHeight) / 2 + "px";
        helpButton.className = 'iconButtonsClicked';
    } else {
        document.getElementById('helpModal').style.display = 'none';
    	document.getElementById('helpModal').classList.remove('show');
        helpButton.className = 'iconButtons';
    }
    HelpModalDisplay_ = !HelpModalDisplay_;
}