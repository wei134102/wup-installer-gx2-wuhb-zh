# 脚本说明（`scripts/`）

本目录存放与本仓库相关的辅助脚本（主要为 **Python 3**）。

---

## 环境要求

- **Python 3.6+**（建议 3.10+）
- 命令行在项目仓库根目录执行时，路径中的 `scripts/` 均相对于该根目录。

---

## `extract_unique_chars.py`

从指定目录下**文本类源文件**中读取 UTF-8 内容，收集其中出现过的**字符**（不是词语），**去重**后按选定规则**排序**，输出为一行或多行字符串（默认一整行连续字符），可用于：

- 整理界面文案用到的字表；
- 交给字体子集工具（如 `pyftsubset` / TTF-Min）作为字符集参考。

### 扫描范围

- **内置扩展名**：`.c` `.cc` `.cpp` `.cxx` `.h` `.hpp` `.hh` `.inl` `.txt` `.md` `.xml` `.json` `.cmake` `.bat` `.ps1` `.sh` `.py` `.rc` `.properties`
- **追加扩展名（无需命令行参数）**：若与本脚本**同目录**存在 **`extensions.txt`**（UTF-8），则将其中的扩展名与内置列表**合并**后再扫描。文件不存在时仅使用内置列表。
- **跳过目录**：`.git` `build` `dist` `node_modules` `.vs` `__pycache__`
- **单文件上限**：4 MiB（更大的文件会被跳过）

### `extensions.txt` 格式（可选）

路径：**`scripts/extensions.txt`**（与 `extract_unique_chars.py` 同级）

- 编码：**UTF-8**
- 分隔符：任意空白（空格、换行、制表）
- 写法：`.lang` 或 `lang`（无前导点时脚本会自动补上）
- 注释：去掉首尾空白后**整行**以 **`#`** 开头的视为注释并忽略；同一行内以 **`#`** 开头的片段也会被忽略

示例见仓库内 **`scripts/extensions.txt`**（默认仅为注释模板，按需取消注释或追加扩展名）。

### 命令行参数

| 参数 | 说明 |
|------|------|
| `--root <路径>` | 扫描根目录，**默认**为仓库根（脚本所在目录的上一级） |
| `--out <文件>` | 将结果写入 UTF-8 文本文件；不写则打印到标准输出 |
| `--skip-whitespace` | 不收集任何空白字符（空格、换行、制表等） |
| `--sort grouped` | **默认**。分段排序：先 ASCII `!`～`~`，再其它 `< U+4E00` 的符号/标点，再 CJK 统一表意区汉字，再扩展字符；控制字符排在最后 |
| `--sort codepoint` | 按 Unicode 码位从小到大一条龙排序 |

### 排序说明（`--sort grouped`）

便于人工核对与复制：大致顺序为「常用 ASCII 符号与字母数字 → 其它标点符号 → 汉字主体区 → 其余」。若未使用 `--skip-whitespace`，Tab/换行等会出现在**最后一段**，避免插在中间。

---

## 常用命令

在项目根目录 `wup-installer-gx2-wuhb-zh` 下：

### Windows（CMD）

```bat
cd /d D:\code\wup-installer-gx2-wuhb-zh

REM 默认：分段排序，输出到控制台（若已编辑 scripts\extensions.txt 会自动合并额外扩展名）
python scripts\extract_unique_chars.py

REM 写入文件（推荐），不含空白字符，便于做字表
python scripts\extract_unique_chars.py --skip-whitespace --out scripts\charset_for_font.txt

REM 扫描其它目录（例如仅某子模块）
python scripts\extract_unique_chars.py --root D:\code\wup-installer-gx2-wuhb-zh\src --out scripts\charset_src_only.txt

REM 纯 Unicode 码位排序
python scripts\extract_unique_chars.py --sort codepoint --out scripts\charset_codepoint.txt

REM 查看帮助
python scripts\extract_unique_chars.py --help
```

### PowerShell

```powershell
Set-Location D:\code\wup-installer-gx2-wuhb-zh
python .\scripts\extract_unique_chars.py --skip-whitespace --out .\scripts\charset_for_font.txt
```

### 与字体精简流程衔接（示例）

将生成的字表复制到 [TTF-Min](https://github.com/ZaneL1u/TTF-Min) 等项目的 `content.txt`，或使用：

```text
pyftsubset input.ttf --text-file=charset_for_font.txt --output-file=output.ttf
```

（需先 `pip install fonttools`。）

---

## 常见问题

**输出里有乱码替换字符（�）**  
说明某些源文件不是合法 UTF-8，解码时已用替换字符占位；应对应文件检查编码或改为 UTF-8 保存。

**想要拼音排序的汉字**  
本脚本仅按 Unicode / 分段规则排序，不做拼音排序；需要可自行在外部用专用工具处理字表。

**需要包含 `.png` / 二进制资源里的「字」**  
本脚本只扫文本扩展名；二进制文件请勿当 UTF-8 全文读取。

---

## 变更记录（脚本维护时可更新）

- `extract_unique_chars.py`：支持 `--sort grouped` / `--sort codepoint`、`--skip-whitespace`；通过 **`scripts/extensions.txt`** 自动追加扩展名（无额外参数）。
