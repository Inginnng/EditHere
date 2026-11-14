# AI analysis (preview, 0.13.0)

[简体中文](AI-INSTRUCTIONS.md)

Desktop EditHere can turn screenshots, component moves and annotations into plain-text modification instructions. Images are processed in a separate API request. Only text is copied to the target agent conversation, reducing image context in that conversation. The vision request still uses your provider's API credits. This is a preview feature: check the model's interpretation before using it.

## Set up once

1. Open **Settings → AI API** and choose a provider.
2. Enter an API key from that provider's standard developer platform. The address, model and protocol are prefilled and can be changed.
3. Click **Test connection**. The test uses the key currently entered and sends only a built-in small image, never the current screenshot.
4. Click **Save settings** at the bottom to save both the key and configuration. Keys go into the system credential store; other options go into ordinary settings. Cancel discards unsaved key drafts.

Settings uses a left navigation sidebar. The AI page has a provider selector with brand icons, with the API key, model and API address always visible; GLM and Kimi offer China and global region selectors. China, global and Coding Plan services may use different accounts and credits. Edit the common settings directly; **Advanced options** contains only the protocol, timeout and additional instructions. You can also choose a custom service. See [provider presets and open-source sources (Chinese)](ai-providers.md). **System prompt** opens the prompt currently in use for direct editing, with a one-click restore to the default; save it, then save settings to apply. Additional instructions can specify an output language or wording. Clear a key and save to delete that provider's stored key.

## Generate and hand off

1. Finish annotating, then click **AI analysis** directly on the editor's bottom toolbar. The same entry is also available in **View JSON**.
2. Check the images, provider and model on the left, then click **Start analysis**. The dialog processes a snapshot taken when it was opened; opening it does not send a request.
3. Review the identified objects, original comments, exact values and items marked for review. You can edit the result directly.
4. Click **Copy text** to hand it to an agent, or **Save as…** to export UTF-8 TXT / Markdown.

Image set analysis sends only pages with annotations or layout adjustments. The result identifies each page by number and name. Feedback about the whole set can be sent as text alone. When a set exceeds the image count or request size limit, EditHere batches it by page, keeps the original and adjusted image together, and combines the results once all batches succeed.

Closing the dialog or clicking **Stop** cancels a running request. Failed, cancelled and timed-out requests are not automatically retried. The original images and annotations remain in the current project; save the project separately to keep them between sessions.

Results correspond to the annotation objects. The app preserves original coordinates, displacement, scaling, video timestamps and the user's exact comments; the model adds object identity, spatial relationships and the modification description. Moves use original-image coordinates; ordinary and child annotations use the adjusted layout's coordinates. Values are image pixels and must not be assumed to be CSS pixels. Each frame with layout changes supplies both the original and a full-resolution adjusted image.

The **⋯** menu sits inside **Copy JSON content** on the editor's bottom toolbar. It controls whether original-image bytes are included, without taking a separate toolbar slot.

## Current limits

An analysis can cover at most 100 objects and 8 video frames. Each request can carry up to 16 images and 32 MiB, with up to 5 MiB per PNG. Providers may impose additional image dimension limits. If the input still exceeds a limit, the app reports an error instead of silently downscaling or dropping pages. A truncated response or a response missing objects is rejected instead of presented as a complete instruction list.

Structural and numeric checks cannot prove that the model understood every visual detail correctly. Review the text before using it. Keep the original project for complex visual changes so the relevant area can be interpreted again if needed.

API keys do not enter the project, annotation JSON, ordinary settings files or diagnostic logs. QtKeychain uses Windows Credential Store, macOS Keychain, or the Linux desktop's credential service. An unavailable credential store produces an explicit error without falling back to plaintext. This feature does not automatically read other agent tools' keys or change their settings.

This version implements the desktop's separate API mode. The MCP mode that delegates interpretation to an independent host-agent conversation is not connected yet.
