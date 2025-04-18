const fs = require("fs");
const path = require("path");
const crypto = require("crypto");
const { execSync } = require("child_process");

function replace_mermaid(lines, target_folder) {
    const result = [];
    let inMermaid = false;
    let currentMermaid = [];
    let diagramCount = 0;

    for (const line of lines) {
        if (line.trim() === "```mermaid") {
            inMermaid = true;
            currentMermaid = [];
        } else if (line.trim() === "```" && inMermaid) {
            inMermaid = false;

            // Mermaid-Inhalt zu eindeutigem Hash
            const content = currentMermaid.join("\n");
            const hash = crypto.createHash("sha256").update(content).digest("hex").substring(0, 8);
            const filename = `mermaid_${hash}.png`;
            const filepath = path.join(target_folder, filename);

            // Falls Bild noch nicht existiert, generieren
            if (!fs.existsSync(filepath)) {
                const tempInputFile = path.join(target_folder, `tmp_${hash}.mmd`);
                fs.writeFileSync(tempInputFile, content, "utf-8");

                try {
                    execSync(`mmdc -i "${tempInputFile}" -o "${filepath}" --backgroundColor transparent --width 1024 -c ${__dirname}/mermaid_config.json`, {
                        stdio: "inherit",
                    });
                } catch (err) {
                    console.error("Mermaid render error:", err);
                } finally {
                    fs.unlinkSync(tempInputFile); // temporäre Datei aufräumen
                }
            }

            // Markdown ersetzen
            result.push(`![](./${filename})`);
        } else if (inMermaid) {
            currentMermaid.push(line);
        } else {
            result.push(line);
        }
    }

    return result;
}

module.exports = { replace_mermaid }