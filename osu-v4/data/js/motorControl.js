// Wait for the DOM to load
document.addEventListener("DOMContentLoaded", () => {
    // Find buttons 
    const forwardBtn = document.getElementById("forwardButton");
    const backwardBtn = document.getElementById("backwardButton");
    const stopBtn = document.getElementById("stopButton");
  
    // Check if buttons exist
    if (forwardBtn && backwardBtn && stopBtn) {
      forwardBtn.addEventListener("click", () => {
        fetch("/move/forward")
          .then(response => response.text())
          .then(text => console.log("Forward:", text))
          .catch(err => console.error("Error:", err));
      });
  
      backwardBtn.addEventListener("click", () => {
        fetch("/move/backward")
          .then(response => response.text())
          .then(text => console.log("Backward:", text))
          .catch(err => console.error("Error:", err));
      });
  
      stopBtn.addEventListener("click", () => {
        fetch("/move/stop")
          .then(response => response.text())
          .then(text => console.log("Stop:", text))
          .catch(err => console.error("Error:", err));
      });
    } else {
      console.error("Motor control buttons not found.");
    }
  });
  