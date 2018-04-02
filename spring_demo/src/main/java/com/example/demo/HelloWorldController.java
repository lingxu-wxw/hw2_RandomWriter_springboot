
package com.example.demo;

import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;
 
@RestController
public class HelloWorldController {
 
    @RequestMapping("/hello")
    public String say(@RequestParam("name") String name){
        return "Hello World"+name;
    }
}