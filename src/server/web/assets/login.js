import { login } from "/assets/auth.js";

const form = document.querySelector("#login-form");
const message = document.querySelector("#message");

form.addEventListener("submit", async (event) => {
    event.preventDefault();
    message.textContent = "页面已接通，登录请求待实现";
});
