function humanFileSize(bytes) {
  if (bytes === 0) {
    return "0 B";
  }

  const k = 1024;
  const sizes = ["B", "KB", "MB"];
  const i = Math.floor(Math.log(bytes) / Math.log(k));

  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + " " + sizes[i];
}

function gifUploadHandler() {
  return {
    async playGifFullscreen(gifName) {
      try {
        await fetch(`/api/v1/gif/play`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ name: gifName }),
        });
      } catch (e) {
        alert("Error when playing gif: " + e);
      }
    },

    async stopGif() {
      try {
        const res = await fetch(`/api/v1/gif/stop`, { method: "POST" });
        if (!res.ok) {
          const txt = await res.text().catch(() => "");
          alert("Failed to stop GIF: " + txt);
        }
      } catch (e) {
        alert("Error when stopping gif: " + e);
      }
    },

    humanFileSize,
    uploading: false,
    uploadMessage: "",
    gifs: [],
    usedBytes: 0,
    totalBytes: 0,
    freeBytes: 0,

    get usedBytesHR() {
      return humanFileSize(this.usedBytes);
    },

    get totalBytesHR() {
      return humanFileSize(this.totalBytes);
    },

    get freeBytesHR() {
      return humanFileSize(this.freeBytes);
    },

    gifListLoaded: false,

    async uploadGif() {
      this.uploading = true;
      this.uploadMessage = "";
      const file = this.$refs.fileInput.files[0];

      if (!file || file.type !== "image/gif") {
        this.uploadMessage = "Please select a GIF file";
        this.uploading = false;

        return;
      }

      const formData = new FormData();
      formData.append("upload", file, file.name);

      try {
        const response = await fetch("/api/v1/gif", {
          method: "POST",
          body: formData,
        });

        const result = await response.json();

        if (result.status === "success") {
          this.uploadMessage = "GIF uploaded: " + result.filename;
          await this.fetchGifList();
        } else {
          this.uploadMessage = result.message || "Upload failed";
        }
      } catch (e) {
        this.uploadMessage = "Error uploading GIF: " + e;
      }

      this.uploading = false;
    },

    async fetchGifList() {
      this.gifListLoaded = false;

      try {
        const response = await fetch("/api/v1/gif");
        const data = await response.json();
        this.gifs = data.files || [];
        this.usedBytes = data.usedBytes || 0;
        this.totalBytes = data.totalBytes || 0;
        this.freeBytes = data.freeBytes || 0;
      } catch (e) {
        this.gifs = [];
        this.usedBytes = this.totalBytes = this.freeBytes = 0;
      }

      this.gifListLoaded = true;
    },

    async init() {
      await this.fetchGifList();
    },
  };
}

function includeHTML(id, url, callback) {
  fetch(url)
    .then((response) => response.text())
    .then((data) => {
      document.getElementById(id).innerHTML = data;
      if (typeof callback === "function") callback();
    });
}

function setHeaderTitle(title) {
  const interval = setInterval(() => {
    const h1 = document.getElementById("header-title");
    if (h1) {
      h1.textContent = title;
      clearInterval(interval);
    }
  }, 20);
}

document.addEventListener("DOMContentLoaded", () => {
  if (document.getElementById("header-placeholder")) {
    includeHTML("header-placeholder", "./header.html", () => {
      let pageTitle =
        document.title && document.title.trim()
          ? document.title.trim()
          : "Placeholder Title";
      setHeaderTitle(pageTitle);
    });
  }
  if (document.getElementById("footer-placeholder")) {
    includeHTML("footer-placeholder", "./footer.html");
  }
});

//
function rebootHandler() {
  return {
    loading: false,
    message: "",
    async reboot() {
      this.loading = true;
      this.message = "Rebooting...";
      try {
        const res = await fetch("/api/v1/reboot", { method: "POST" });
        if (res.ok) {
          this.message = "Device is rebooting...";
        } else {
          this.message = "Reboot failed!";
        }
      } catch (e) {
        this.message = "Error: " + e;
      }
      setTimeout(() => {
        this.loading = false;
      }, 5000);
    },
  };
}
function themeSwitcher() {
  function getSVGElements() {
    return {
      mainCircle: document.getElementById("theme-main-circle"),
      sunRays: document.getElementById("theme-sun-rays"),
      moon: document.getElementById("theme-moon"),
      moonCut: document.getElementById("moon-cut"),
      rays: Array.from(document.querySelectorAll("#theme-sun-rays circle")),
      themeBtn: document.querySelector(".theme-switch-btn"),
      overlay: document.getElementById("theme-waterdrop-overlay"),
    };
  }

  function playWaterDropTransition(event, nextTheme) {
    const { overlay } = getSVGElements();
    if (!overlay) return;
    const startX = window.innerWidth;
    const startY = 0;
    const endX = 0;
    const endY = window.innerHeight;
    const maxRadius = Math.sqrt(
      window.innerWidth ** 2 + window.innerHeight ** 2
    );
    const bg = nextTheme === "dark" ? "#111" : "#fff";
    overlay.style.setProperty("--theme-waterdrop-bg", bg);
    overlay.style.clipPath = `circle(0% at ${startX}px ${startY}px)`;
    overlay.classList.add("active");
    overlay.style.opacity = "1";
    void overlay.offsetWidth;
    overlay.style.transition = "clip-path 0.7s cubic-bezier(.4,0,.2,1)";
    overlay.style.clipPath = `circle(${
      maxRadius + 50
    }px at ${endX}px ${endY}px)`;
    setTimeout(() => {
      document.documentElement.setAttribute("data-theme", nextTheme);
      localStorage.setItem("picoPreferredColorScheme", nextTheme);
      if (window.Alpine && Alpine.store && Alpine.store.themeSwitcher) {
        Alpine.store.themeSwitcher.applyScheme();
      } else if (
        window.themeSwitcherInstance &&
        typeof window.themeSwitcherInstance.applyScheme === "function"
      ) {
        window.themeSwitcherInstance.applyScheme();
      } else {
        const { mainCircle, sunRays, moon, moonCut, rays } = getSVGElements();
        if (nextTheme === "light") {
          mainCircle && (mainCircle.style.opacity = 1);
          sunRays && (sunRays.style.opacity = 1);
          rays && rays.forEach((ray) => (ray.style.opacity = 1));
          moon && (moon.style.display = "none");
          moonCut && moonCut.setAttribute("cx", 24);
        } else {
          mainCircle && (mainCircle.style.opacity = 0);
          sunRays && (sunRays.style.opacity = 0);
          rays && rays.forEach((ray) => (ray.style.opacity = 0));
          moon && (moon.style.display = "");
          moonCut && moonCut.setAttribute("cx", 16);
        }
      }
      overlay.style.transition = "opacity 0.3s";
      overlay.style.opacity = "0";
      setTimeout(() => {
        overlay.classList.remove("active");
        overlay.style.clipPath = "";
        overlay.style.transition = "";
      }, 300);
    }, 700);
  }

  return {
    scheme: localStorage.getItem("picoPreferredColorScheme") || "auto",
    get effectiveScheme() {
      if (this.scheme === "auto") {
        return window.matchMedia("(prefers-color-scheme: dark)").matches
          ? "dark"
          : "light";
      }
      return this.scheme;
    },
    setScheme(scheme) {
      this.scheme = scheme;
      localStorage.setItem("picoPreferredColorScheme", scheme);
      this.applyScheme();
    },
    applyScheme() {
      document.documentElement.setAttribute("data-theme", this.effectiveScheme);
      const { mainCircle, sunRays, moon, moonCut, rays } = getSVGElements();
      if (!mainCircle || !sunRays || !moon || !moonCut) return;
      if (this.effectiveScheme === "light") {
        mainCircle.style.opacity = 1;
        sunRays.style.opacity = 1;
        rays.forEach((ray) => (ray.style.opacity = 1));
        moon.style.display = "none";
        moonCut.setAttribute("cx", 24);
      } else {
        mainCircle.style.opacity = 0;
        sunRays.style.opacity = 0;
        rays.forEach((ray) => (ray.style.opacity = 0));
        moon.style.display = "";
        moonCut.setAttribute("cx", 16);
      }
    },
    toggleTheme(event) {
      const current = this.effectiveScheme;
      const next = current === "dark" ? "light" : "dark";
      playWaterDropTransition(event, next);
      this.scheme = next;
    },
    init() {
      const stored = localStorage.getItem("picoPreferredColorScheme");
      this.scheme = stored ? stored : "auto";
      this.applyScheme();
      window
        .matchMedia("(prefers-color-scheme: dark)")
        .addEventListener("change", (e) => {
          if (this.scheme === "auto") this.applyScheme();
        });
    },
  };
}

function otaUploadHandler() {
  return {
    uploading: false,
    uploadMessage: "",
    uploadType: "firmware",

    async uploadFile() {
      const fileInput = this.$refs.fileInput;
      if (!fileInput.files.length) {
        this.uploadMessage = "Please select a file";

        return;
      }

      const file = fileInput.files[0];
      if (!file.name.toLowerCase().endsWith(".bin")) {
        this.uploadMessage = "Only .bin files are allowed";

        return;
      }
      const endpoint =
        this.uploadType === "firmware" ? "/api/v1/ota/fw" : "/api/v1/ota/fs";

      this.uploading = true;
      this.uploadMessage = "";

      try {
        const formData = new FormData();
        formData.append("file", file);
        const res = await fetch(endpoint, {
          method: "POST",
          body: formData,
        });

        if (res.ok) {
          const data = await res.json().catch(() => ({}));
          this.uploadMessage = data.message;
        } else {
          const err = await res.text();
          this.uploadMessage = "Error: " + err;
        }
      } catch (e) {
        this.uploadMessage = "Error: " + e;
      }

      this.uploading = false;
    },
  };
}

document.addEventListener("alpine:init", () => {
  Alpine.data("themeSwitcher", themeSwitcher);
  Alpine.data("otaUploadHandler", otaUploadHandler);
  Alpine.data("gifUploadHandler", gifUploadHandler);
});
