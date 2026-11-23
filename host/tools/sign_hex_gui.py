"""Window that signs a tree of Keil HEX files and publishes the bins.

Layout, both locally and on the server:

    <root>/<device type>/<version>_....hex
    <root>/<device type>/<version>_0x<flash base>_<signature>.bin

The version is the four numbers at the start of the hex name. The signature
is the same one hex_to_ota.py embeds in the filename. One version keeps one
bin: an older file for that version is removed when a new hex is signed.
"""
import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from hex_to_ota import FILENAME_VERSION_RE, parse_intel_hex
from sign_firmware import KEY_PATH, sign_image

DEFAULT_HEX = Path(r"C:\source\compiled\hex")
DEFAULT_BIN = Path(r"C:\source\compiled\bin")
DEFAULT_HOST = "46.17.47.109"
DEFAULT_USER = "root"
DEFAULT_REMOTE = "/opt/timberline-web/public/firmware"
DEFAULT_KEY = Path(r"C:\Users\miai\Desktop\SSH\timberline_mqtt_vps_KEY")


def bundled_key() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys._MEIPASS) / "firmware-ed25519.pem"
    return KEY_PATH


def version_from_name(path: str) -> str:
    match = FILENAME_VERSION_RE.match(Path(path).name)
    if not match:
        return ""
    return ".".join(match.groups())


def convert(hex_path: str, version: str, out_dir: str) -> Path:
    version = version.strip()
    if not version:
        raise ValueError("Укажите версию, например 121.0.0.30")
    base, image = parse_intel_hex(hex_path)
    sig = sign_image(version, base, image, bundled_key())
    name = f"{version}_0x{base:08X}_{sig}.bin"
    dest_dir = Path(out_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)
    dest = dest_dir / name
    for old in dest_dir.glob(f"{version}_0x*.bin"):
        if old != dest:
            old.unlink()
    dest.write_bytes(image)
    return dest


def iter_hex(hex_root: Path):
    for type_dir in sorted(p for p in hex_root.iterdir() if p.is_dir()):
        if not type_dir.name.isdigit():
            continue
        for hex_path in sorted(type_dir.glob("*.hex")):
            yield type_dir.name, hex_path


def convert_tree(hex_root: Path, bin_root: Path, log) -> tuple[int, int]:
    if not bundled_key().is_file():
        raise FileNotFoundError("Ключ подписи не найден")
    converted = skipped = 0
    seen = {}
    for dev_type, hex_path in iter_hex(hex_root):
        version = version_from_name(hex_path.name)
        if not version:
            raise ValueError(f"{hex_path.name}: имя должно начинаться с версии, например 121.0.0.30_....hex")
        if version.split(".")[0] != dev_type:
            raise ValueError(f"{hex_path.name}: версия не совпадает с папкой {dev_type}")
        key = (dev_type, version)
        if key in seen:
            log(f"повтор версии {version}, берётся {hex_path.name}")
        seen[key] = hex_path
        out_dir = bin_root / dev_type
        dest_name_glob = list(out_dir.glob(f"{version}_0x*.bin")) if out_dir.is_dir() else []
        # Sign first into a temp comparison by always signing: same hex gives
        # the same name, so an existing file of that name is already current.
        base, image = parse_intel_hex(str(hex_path))
        sig = sign_image(version, base, image, bundled_key())
        name = f"{version}_0x{base:08X}_{sig}.bin"
        dest = out_dir / name
        if dest.is_file() and dest.stat().st_size == len(image):
            for old in dest_name_glob:
                if old != dest:
                    old.unlink()
                    log(f"убран старый {old.name}")
            skipped += 1
            continue
        out_dir.mkdir(parents=True, exist_ok=True)
        for old in out_dir.glob(f"{version}_0x*.bin"):
            if old.name != name:
                old.unlink()
                log(f"убран старый {old.name}")
        dest.write_bytes(image)
        log(f"{dev_type}/{name}  {len(image)} байт")
        converted += 1
    return converted, skipped


def local_bins(bin_root: Path) -> dict[str, Path]:
    found = {}
    if not bin_root.is_dir():
        return found
    for type_dir in sorted(p for p in bin_root.iterdir() if p.is_dir()):
        for bin_path in sorted(type_dir.glob("*.bin")):
            rel = f"{type_dir.name}/{bin_path.name}"
            found[rel.replace("\\", "/")] = bin_path
    return found


def ssh_cmd(key: Path) -> list[str]:
    return [
        "ssh", "-i", str(key),
        "-o", "IdentitiesOnly=yes",
        "-o", "StrictHostKeyChecking=yes",
    ]


def scp_cmd(key: Path) -> list[str]:
    return [
        "scp", "-i", str(key),
        "-o", "IdentitiesOnly=yes",
        "-o", "StrictHostKeyChecking=yes",
    ]


def run_checked(argv: list[str], log) -> str:
    proc = subprocess.run(argv, capture_output=True, text=True)
    if proc.returncode != 0:
        detail = (proc.stderr or proc.stdout or "").strip()
        raise RuntimeError(detail or f"команда завершилась с кодом {proc.returncode}")
    return proc.stdout


def deploy_tree(bin_root: Path, user: str, host: str, remote: str, key: Path, log) -> tuple[int, int]:
    files = local_bins(bin_root)
    if not files:
        raise ValueError("В папке bin нет файлов")
    target = f"{user}@{host}"
    remote = remote.rstrip("/")
    types = sorted({rel.split("/", 1)[0] for rel in files})
    mkdir = " ".join(f"'{remote}/{t}'" for t in types)
    run_checked(ssh_cmd(key) + [target, f"mkdir -p {mkdir}"], log)
    for rel, path in files.items():
        dev_type, name = rel.split("/", 1)
        run_checked(scp_cmd(key) + [str(path), f"{target}:{remote}/{dev_type}/{name}"], log)
        log(f"залит {rel}")
    listing = run_checked(
        ssh_cmd(key) + [target, f"find '{remote}' -type f -name '*.bin'"],
        log,
    )
    remote_names = set()
    for line in listing.splitlines():
        line = line.strip()
        if not line.startswith(remote + "/"):
            continue
        remote_names.add(line[len(remote) + 1:])
    extra = sorted(remote_names - set(files))
    for rel in extra:
        run_checked(ssh_cmd(key) + [target, f"rm -f '{remote}/{rel}'"], log)
        log(f"с сервера удалён {rel}")
    return len(files), len(extra)


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Прошивки: подпись и публикация")
        self.resizable(False, False)
        pad = {"padx": 8, "pady": 4}
        frm = ttk.Frame(self)
        frm.grid(row=0, column=0, sticky="nsew")

        self.hex_var = tk.StringVar(value=str(DEFAULT_HEX))
        self.bin_var = tk.StringVar(value=str(DEFAULT_BIN))
        self.host_var = tk.StringVar(value=DEFAULT_HOST)
        self.user_var = tk.StringVar(value=DEFAULT_USER)
        self.remote_var = tk.StringVar(value=DEFAULT_REMOTE)
        self.key_var = tk.StringVar(value=str(DEFAULT_KEY))

        rows = [
            ("Папка HEX", self.hex_var, self.pick_hex),
            ("Папка BIN", self.bin_var, self.pick_bin),
            ("SSH-ключ", self.key_var, self.pick_key),
        ]
        for i, (label, var, cmd) in enumerate(rows):
            ttk.Label(frm, text=label).grid(row=i, column=0, sticky="w", **pad)
            ttk.Entry(frm, textvariable=var, width=72).grid(row=i, column=1, **pad)
            ttk.Button(frm, text="Обзор…", command=cmd).grid(row=i, column=2, **pad)

        ttk.Label(frm, text="Сервер").grid(row=3, column=0, sticky="w", **pad)
        host = ttk.Frame(frm)
        host.grid(row=3, column=1, sticky="w", **pad)
        ttk.Entry(host, textvariable=self.user_var, width=12).grid(row=0, column=0)
        ttk.Label(host, text="@").grid(row=0, column=1)
        ttk.Entry(host, textvariable=self.host_var, width=22).grid(row=0, column=2, padx=(0, 8))
        ttk.Entry(host, textvariable=self.remote_var, width=42).grid(row=0, column=3)

        buttons = ttk.Frame(frm)
        buttons.grid(row=4, column=1, sticky="w", **pad)
        self.convert_btn = ttk.Button(buttons, text="Преобразовать HEX в BIN", command=self.on_convert)
        self.convert_btn.grid(row=0, column=0, padx=(0, 8))
        self.deploy_btn = ttk.Button(buttons, text="Залить BIN на сервер", command=self.on_deploy)
        self.deploy_btn.grid(row=0, column=1)

        self.log = tk.Text(frm, width=88, height=16, state="disabled")
        self.log.grid(row=5, column=0, columnspan=3, padx=8, pady=8)
        self.write("Ключ подписи найден" if bundled_key().is_file() else "Ключ подписи не найден")
        self.write("HEX: папка/тип/121.0.0.30_....hex")
        self.write("BIN: папка/тип/121.0.0.30_0x........_<подпись>.bin")

    def write(self, text: str):
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def log_from_worker(self, text: str):
        self.after(0, lambda: self.write(text))

    def pick_hex(self):
        path = filedialog.askdirectory(title="Папка с HEX, внутри папки типов")
        if path:
            self.hex_var.set(path)

    def pick_bin(self):
        path = filedialog.askdirectory(title="Папка для подписанных BIN")
        if path:
            self.bin_var.set(path)

    def pick_key(self):
        path = filedialog.askopenfilename(title="Закрытый SSH-ключ")
        if path:
            self.key_var.set(path)

    def set_busy(self, busy: bool):
        state = "disabled" if busy else "normal"
        self.convert_btn.configure(state=state)
        self.deploy_btn.configure(state=state)

    def on_convert(self):
        hex_root = Path(self.hex_var.get().strip())
        bin_root = Path(self.bin_var.get().strip())
        if not hex_root.is_dir():
            messagebox.showerror("Нет папки", f"Не найдена папка HEX:\n{hex_root}")
            return
        self.set_busy(True)

        def work():
            try:
                converted, skipped = convert_tree(hex_root, bin_root, self.log_from_worker)
                self.log_from_worker(f"Готово: подписано {converted}, уже были {skipped}")
            except Exception as exc:
                self.log_from_worker(str(exc))
                self.after(0, lambda: messagebox.showerror("Не удалось подписать", str(exc)))
            finally:
                self.after(0, lambda: self.set_busy(False))

        threading.Thread(target=work, daemon=True).start()

    def on_deploy(self):
        bin_root = Path(self.bin_var.get().strip())
        key = Path(self.key_var.get().strip())
        user = self.user_var.get().strip()
        host = self.host_var.get().strip()
        remote = self.remote_var.get().strip()
        if not bin_root.is_dir() or not key.is_file() or not user or not host or not remote:
            messagebox.showerror("Не хватает данных", "Нужны папка BIN, SSH-ключ, пользователь, адрес и путь на сервере")
            return
        files = local_bins(bin_root)
        if not messagebox.askyesno(
            "Залить на сервер",
            f"Будет залито файлов: {len(files)}.\n"
            "Файл на сервере, которого нет в папке BIN, будет удалён.",
        ):
            return
        self.set_busy(True)

        def work():
            try:
                uploaded, removed = deploy_tree(bin_root, user, host, remote, key, self.log_from_worker)
                self.log_from_worker(f"Готово: залито {uploaded}, удалено лишних {removed}")
            except Exception as exc:
                self.log_from_worker(str(exc))
                self.after(0, lambda: messagebox.showerror("Не удалось залить", str(exc)))
            finally:
                self.after(0, lambda: self.set_busy(False))

        threading.Thread(target=work, daemon=True).start()


def main():
    if len(sys.argv) > 1:
        raise SystemExit(main_cli(sys.argv))
    App().mainloop()


def main_cli(argv: list[str]) -> int:
    if argv[1] == "convert" and len(argv) == 4:
        converted, skipped = convert_tree(Path(argv[2]), Path(argv[3]), print)
        print(f"signed {converted}, kept {skipped}")
        return 0
    if len(argv) >= 4 and argv[1] != "convert":
        print(convert(argv[1], argv[2], argv[3]))
        return 0
    print("usage: FirmwarePublish convert <hex-root> <bin-root>")
    print("       FirmwarePublish <file.hex> <version> <out-dir>")
    return 2


if __name__ == "__main__":
    main()
