package com.ezquest.engine;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/**
 * Per-profile extra engine args, validated SourceVR-style.
 *
 * Ported from SourceVR 0.1.25
 * ({@code com.sourcevrport.hl2vr.ProfileLaunchArguments}, decompiled
 * 2026-09-24 from {@code SourceVR-0.1.25.apk}) so EZQuest accepts the same
 * tuning knobs and rejects the same footguns:
 *
 * <ul>
 *   <li>max 1024 UTF-8 bytes, max 128 whitespace-separated tokens</li>
 *   <li>no double-quotes, no control characters, no lone surrogates</li>
 *   <li>no {@code @response} files</li>
 *   <li>managed args are blocked (the launcher owns them):
 *       {@code -game -basedir -defaultgamedir -vproject -insert_search_path
 *       -w -width -h -height -vr -novr -heapsize -mobile -nosteam -steam
 *       -insecure -secure}</li>
 *   <li>{@code +vr_eye_resolution} must be one of
 *       {@code auto 2064x2208 1680x1760 1548x1656 1032x1104 2880x3016}</li>
 *   <li>{@code +vr_compositor_sharpening} must be one of
 *       {@code 0 off 1 normal 2 quality}</li>
 * </ul>
 *
 * The file itself lives at {@code <filesRoot>/launch-args/<profile>.txt}
 * (see {@link EngineEnv}); this class only validates/normalizes the text.
 * Validation is fail-closed: callers must not publish or forward args that
 * do not validate.
 */
public final class ProfileLaunchArguments {
    public static final int MAX_TOKENS = 128;
    public static final int MAX_UTF8_BYTES = 1024;

    /** Launcher-owned flags; user launch-args may not contain these. */
    public static final Set<String> MANAGED_ARGUMENTS = unmodifiableLowercaseSet(
            "-game", "-basedir", "-defaultgamedir", "-vproject",
            "-insert_search_path", "-w", "-width", "-h", "-height",
            "-vr", "-novr", "-heapsize", "-mobile", "-nosteam", "-steam",
            "-insecure", "-secure");

    /** Allowed values for +vr_eye_resolution (lowercase). */
    public static final Set<String> EYE_RESOLUTIONS = unmodifiableLowercaseSet(
            "auto", "2064x2208", "1680x1760", "1548x1656",
            "1032x1104", "2880x3016");

    /** Allowed values for +vr_compositor_sharpening (lowercase). */
    public static final Set<String> SHARPENING_MODES = unmodifiableLowercaseSet(
            "0", "off", "1", "normal", "2", "quality");

    public enum Error {
        NONE,
        QUOTES,
        INVALID_UNICODE,
        CONTROL_CHARACTER,
        TOO_LARGE,
        TOO_MANY_TOKENS,
        RESPONSE_FILE,
        MANAGED_ARGUMENT,
        INVALID_EYE_RESOLUTION,
        INVALID_SHARPENING
    }

    public static final class Validation {
        public final Error error;
        /** Offending token/value, or "" when not applicable. */
        public final String arg;
        /** Normalized args (valid only when {@link #isValid()}). */
        public final String normalized;
        public final int byteLength;
        public final int tokenCount;

        Validation(Error error, String arg, String normalized,
                   int byteLength, int tokenCount) {
            this.error = error;
            this.arg = arg == null ? "" : arg;
            this.normalized = normalized == null ? "" : normalized;
            this.byteLength = byteLength;
            this.tokenCount = tokenCount;
        }

        public boolean isValid() {
            return error == Error.NONE;
        }
    }

    private ProfileLaunchArguments() {
    }

    private static Set<String> unmodifiableLowercaseSet(String... values) {
        Set<String> out = new HashSet<String>(Arrays.asList(values));
        return Collections.unmodifiableSet(out);
    }

    private static Validation invalid(Error error, String arg, StringBuilder normalized) {
        String text = normalized.toString();
        return new Validation(error, arg, text,
                text.getBytes(StandardCharsets.UTF_8).length,
                countTokens(text));
    }

    private static int countTokens(String text) {
        if (text == null || text.isEmpty()) {
            return 0;
        }
        int tokens = 1;
        for (int i = 0; i < text.length(); i++) {
            if (text.charAt(i) == ' ') {
                tokens++;
            }
        }
        return tokens;
    }

    /**
     * Validate and normalize user launch-args. Never returns null.
     * {@code null} input is treated as "" (valid, empty).
     */
    public static Validation validate(String input) {
        String src = input == null ? "" : input;
        StringBuilder out = new StringBuilder(src.length());
        boolean pendingSpace = false;

        for (int i = 0; i < src.length(); i++) {
            char c = src.charAt(i);
            if (c == '"') {
                return invalid(Error.QUOTES, "\"", out);
            }
            if (Character.isHighSurrogate(c)) {
                if (i + 1 >= src.length()
                        || !Character.isLowSurrogate(src.charAt(i + 1))) {
                    return invalid(Error.INVALID_UNICODE, "", out);
                }
                if (pendingSpace && out.length() > 0) {
                    out.append(' ');
                }
                out.append(c);
                out.append(src.charAt(i + 1));
                i++;
                pendingSpace = false;
                continue;
            }
            if (Character.isLowSurrogate(c)) {
                return invalid(Error.INVALID_UNICODE, "", out);
            }
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                if (out.length() > 0) {
                    pendingSpace = true;
                }
                continue;
            }
            if (c < 0x20) {
                return invalid(Error.CONTROL_CHARACTER,
                        String.format(Locale.ROOT, "U+%04X", (int) c), out);
            }
            if (pendingSpace && out.length() > 0) {
                out.append(' ');
            }
            out.append(c);
            pendingSpace = false;
        }

        String normalized = out.toString();
        byte[] utf8 = normalized.getBytes(StandardCharsets.UTF_8);
        if (utf8.length > MAX_UTF8_BYTES) {
            return new Validation(Error.TOO_LARGE, "", normalized,
                    utf8.length, countTokens(normalized));
        }

        String[] tokens;
        if (normalized.isEmpty()) {
            tokens = new String[0];
        } else {
            tokens = normalized.split(" ", -1);
        }
        if (tokens.length > MAX_TOKENS) {
            return new Validation(Error.TOO_MANY_TOKENS, "", normalized,
                    utf8.length, tokens.length);
        }

        for (String token : tokens) {
            String lower = token.toLowerCase(Locale.ROOT);
            if (token.startsWith("@")) {
                return new Validation(Error.RESPONSE_FILE, token, normalized,
                        utf8.length, tokens.length);
            }
            if (MANAGED_ARGUMENTS.contains(lower)) {
                return new Validation(Error.MANAGED_ARGUMENT, token, normalized,
                        utf8.length, tokens.length);
            }
        }

        for (int i = 0; i < tokens.length; i++) {
            String lower = tokens[i].toLowerCase(Locale.ROOT);
            boolean isEye = "+vr_eye_resolution".equals(lower);
            boolean isSharpen = "+vr_compositor_sharpening".equals(lower);
            if (!isEye && !isSharpen) {
                continue;
            }
            String value = (i + 1 < tokens.length) ? tokens[i + 1] : "";
            boolean missing = value.isEmpty()
                    || value.startsWith("+") || value.startsWith("-");
            if (isEye) {
                if (missing || !EYE_RESOLUTIONS.contains(
                        value.toLowerCase(Locale.ROOT))) {
                    return new Validation(Error.INVALID_EYE_RESOLUTION, value,
                            normalized, utf8.length, tokens.length);
                }
            } else {
                if (missing || !SHARPENING_MODES.contains(
                        value.toLowerCase(Locale.ROOT))) {
                    return new Validation(Error.INVALID_SHARPENING, value,
                            normalized, utf8.length, tokens.length);
                }
            }
        }

        return new Validation(Error.NONE, "", normalized,
                utf8.length, tokens.length);
    }

    /** True when {@code text} is non-null and validates clean. */
    public static boolean isValid(String text) {
        return validate(text).isValid();
    }
}
