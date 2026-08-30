# Changelog

## 1.0.0 — 2026-08-30

First public build of EDITY.

### Connect and workspace
- Named FTP / FTPS / SFTP profiles with passwords in Windows Credential Manager
- Browse remote folders and test a link before you pull
- Pull Market, Traders, and TraderZones JSON in one connect
- Pull mission types from `cfgeconomycore.xml` plus vanilla `db/types.xml`
- Local workspace and dated zip backups under `%APPDATA%\EDITY`

### Market
- Category files, item table, and inspector in one view
- Multi-select, Shift/Ctrl, bulk price/stock edits, infinite stock
- Add items from the types catalog; hide classnames already used (including variants)
- Attachments accept any type; lint offers **Add to market…** when the classname is missing
- Make selected rows variants of another item in the same file
- Color picker (8-digit Expansion hex) and official Expansion icon picker

### Traders and zones
- Trader display name, currencies, reputation, factions, category stems, item overrides
- Expansion v13 files keep unknown keys and do not require `TraderName`
- Zone position, radius, buy/sell percents, and stock map
- Referential checks against market classnames

### Types
- Edit pulled CE `types.xml` files (nominal, min, lifetime, restock, quant, cost, flags, usage, value, tags)
- Search, A–Z sort, quick filters, bulk edit, duplicate, disable (nominal 0)
- Create a types file and register it in `cfgeconomycore.xml`
- Save, lint, and upload on the same path as JSON files

### Lint and upload
- Hard lint for empty names, duplicates, price/stock bounds, min &gt; nominal, missing market references
- Click an issue to jump to the row; **Add to market…** for missing classnames
- `ZmbF_*` and `ZmbM_*` types are skipped; missing category is not a warning
- **Save + lint** is local only
- **Upload all changes** zips a backup, overwrites dirty files, then re-pulls
- File-list and item-table scroll stay put when you click or multi-select
